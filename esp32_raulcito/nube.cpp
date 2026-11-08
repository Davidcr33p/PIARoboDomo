// ===== Página de internet (Firebase Realtime Database) =====
// Rutas que usa el robot (ver database.rules.json):
//   /robot/estado            <- sube sensores y estados cada 1 s
//   /robot/eventos/0..19     <- últimos 20 eventos (buffer circular)
//   /robot/ordenes           -> escucha comando, config y secretos (stream SSE)
//   /robot/respuesta         <- confirma cada comando
//   /solicitudes/{uid}       <- pide vinculación con un código de 6 dígitos
#include "nube.h"
#include "config.h"
#include "portal.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>

// Datos públicos del proyecto (no son secretos: el acceso lo controlan las reglas)
#define FB_API_KEY "AIzaSyBYNofyfHp4aISDo_5hb43kZHwGErLtNhc"
#define FB_HOST    "raulcito-bracito-default-rtdb.firebaseio.com"
#define FB_DOMINIO "raulcito-bracito.web.app"   // dominio del correo de la cuenta del robot

enum EstadoNube { N_APAGADA, N_CONECTANDO, N_SIN_VINCULAR, N_CONECTADA, N_ERROR };
static volatile EstadoNube estado = N_APAGADA;

// Cuenta propia del robot: se crea sola la primera vez y se guarda en la NVS
static String correo, clave;
static char codigo[7] = "";
static String idToken, refreshToken, uid;
static unsigned long tExpira = 0;

static NetworkClientSecure tlsRest, tlsSse;   // validan HTTPS con los certificados del core
static String hostRest;

// Comunicación entre la tarea de la nube y el loop
struct Orden      { char cmd[16]; int64_t n; };
struct Respuesta  { int64_t n; bool ok; char error[48]; };
struct EventoNube { char tipo[24]; char valor[64]; char fecha[20]; };
static QueueHandle_t colaOrdenes = nullptr, colaRespuestas = nullptr, colaEventos = nullptr;
static SemaphoreHandle_t candado = nullptr;
static SemaphoreHandle_t candadoHttp = nullptr;
static String estadoJson;                 // último estado armado por el loop
static bool estadoNuevo = false;
static Config configNube;                 // configuración que llegó de la página
static volatile bool aplicarConfig = false;

// Stream de órdenes
static String lineaSse, eventoSse;
static unsigned long tDatoSse = 0;
static int64_t ultimaOrden = 0;
static bool ordenesVistas = false;        // el primer stream del arranque solo marca la orden actual
static unsigned long tRevision = 0;       // revisar config de la página (0 = nada pendiente)
static uint32_t eventosN = 0;
static bool eventosNLeido = false;

// ---------- IDENTIDAD ----------
static void cargarIdentidad() {
  Preferences p;
  p.begin("nube", false);                 // aparte de "raulcito": sobrevive al restablecer
  correo = p.getString("correo", "");
  clave = p.getString("clave", "");
  strlcpy(codigo, p.getString("codigo", "").c_str(), sizeof(codigo));
  p.end();
}

static void guardarIdentidad() {
  Preferences p;
  p.begin("nube", false);
  p.putString("correo", correo);
  p.putString("clave", clave);
  p.putString("codigo", codigo);
  p.end();
}

static void crearIdentidad() {
  static const char ALFA[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[64];
  snprintf(buf, sizeof(buf), "esp32-%02x%02x%02x-%04x@" FB_DOMINIO, mac[3], mac[4], mac[5],
           (unsigned)(esp_random() & 0xffff));
  correo = buf;
  clave = "";
  for (int i = 0; i < 24; i++) clave += ALFA[esp_random() % (sizeof(ALFA) - 1)];
  snprintf(codigo, sizeof(codigo), "%06u", (unsigned)(esp_random() % 1000000));
}

// ---------- DIAGNÓSTICO ----------
// Imprime un problema solo cuando cambia, para no llenar el monitor serie
static void avisar(const String &msg) {
  static String anterior;
  if (msg == anterior) return;
  anterior = msg;
  Serial.printf("[NUBE] %s · memoria libre %lu (bloque mayor %lu)\n", msg.c_str(),
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
}

static String errorTls(NetworkClientSecure &tls) {
  char buf[100] = "";
  int e = tls.lastError(buf, sizeof(buf));
  return e ? String(buf) + " (" + e + ")" : String("sin detalle");
}

static String horaLocalTexto() {
  time_t ahora = time(nullptr);
  if (ahora < 1700000000) return "NTP pendiente";
  tm local;
  localtime_r(&ahora, &local);
  char texto[24];
  strftime(texto, sizeof(texto), "%Y-%m-%d %H:%M:%S", &local);
  return String(texto);
}

// ---------- HTTPS ----------
static String urlDb(const String &ruta, bool silencioso = false) {
  return "https://" FB_HOST + ruta + ".json?auth=" + idToken + (silencioso ? "&print=silent" : "");
}

static int pedir(const char *metodo, const String &url, const String &cuerpo, String *respuesta,
                 const char *tipo = "application/json") {
  if (candadoHttp) xSemaphoreTake(candadoHttp, portMAX_DELAY);
  String host = url.substring(8, url.indexOf('/', 8));
  if (host != hostRest) { tlsRest.stop(); hostRest = host; }   // no reusar la conexión de otro host
  HTTPClient http;
  // No conservar otra sesión TLS además del stream SSE; el ESP32 necesita
  // memoria contigua para verificar la firma RSA del certificado.
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(6000);
  http.setTimeout(6000);
  if (!http.begin(tlsRest, url)) {
    if (candadoHttp) xSemaphoreGive(candadoHttp);
    return -1;
  }
  http.addHeader("Content-Type", tipo);
  int codigoHttp = http.sendRequest(metodo, cuerpo);
  String detalleTls = codigoHttp < 0 ? errorTls(tlsRest) : "";
  String r = codigoHttp > 0 ? http.getString() : "";
  if (respuesta) *respuesta = r;
  http.end();
  tlsRest.stop();
  if (candadoHttp) xSemaphoreGive(candadoHttp);
  if (codigoHttp < 200 || codigoHttp >= 300) {
    int a = url.indexOf('/', 8), b = url.indexOf('?');
    String ruta = url.substring(a, b > a ? b : url.length());   // sin la clave ni el token
    String detalle;
    if (codigoHttp < 0) {
      detalle = HTTPClient::errorToString(codigoHttp) + " / TLS: " + detalleTls;
    } else {
      JsonDocument errorDoc;
      if (!deserializeJson(errorDoc, r) && errorDoc["error"].is<const char *>())
        detalle = errorDoc["error"].as<String>();
      if (detalle.isEmpty()) {
        detalle = r;
        detalle.replace('\r', ' ');
        detalle.replace('\n', ' ');
        if (detalle.length() > 120) detalle.remove(120);
      }
      if (detalle.isEmpty()) detalle = String(codigoHttp);
    }
    avisar(String(metodo) + " " + ruta + " -> " + String(codigoHttp) + " · " + detalle);
  }
  return codigoHttp;
}

static bool exito(int codigoHttp) { return codigoHttp >= 200 && codigoHttp < 300; }

// Inicia sesión con la cuenta del robot (o la crea la primera vez)
static bool autenticar() {
  JsonDocument in;
  String resp;
  if (refreshToken.length()) {            // renovar sin volver a mandar la contraseña
    int c = pedir("POST", "https://securetoken.googleapis.com/v1/token?key=" FB_API_KEY,
                  "grant_type=refresh_token&refresh_token=" + refreshToken, &resp,
                  "application/x-www-form-urlencoded");
    if (c == 200 && !deserializeJson(in, resp)) {
      idToken = in["id_token"].as<String>();
      refreshToken = in["refresh_token"].as<String>();
      uid = in["user_id"].as<String>();
      tExpira = millis() + (in["expires_in"].as<String>().toInt() - 300) * 1000UL;
      return true;
    }
    refreshToken = "";
  }

  bool nueva = correo.isEmpty();
  if (nueva) crearIdentidad();
  for (int intento = 0; intento < 3; intento++) {
    JsonDocument q;
    q["email"] = correo;
    q["password"] = clave;
    q["returnSecureToken"] = true;
    String cuerpo;
    serializeJson(q, cuerpo);
    String url = String("https://identitytoolkit.googleapis.com/v1/accounts:") +
                 (nueva ? "signUp" : "signInWithPassword") + "?key=" FB_API_KEY;
    int c = pedir("POST", url, cuerpo, &resp);
    in.clear();
    deserializeJson(in, resp);
    if (c == 200) {
      idToken = in["idToken"].as<String>();
      refreshToken = in["refreshToken"].as<String>();
      uid = in["localId"].as<String>();
      tExpira = millis() + (in["expiresIn"].as<String>().toInt() - 300) * 1000UL;
      if (nueva) {
        guardarIdentidad();
        Serial.println("[NUBE] Cuenta del robot creada");
      }
      return true;
    }
    String err = in["error"]["message"] | "";
    Serial.println("[NUBE] Error de cuenta: " + (err.length() ? err : String(c)));
    // La cuenta ya no existe o el correo está ocupado: crea una identidad nueva
    if (err.startsWith("EMAIL_NOT_FOUND") || err.startsWith("INVALID_LOGIN") || err.startsWith("EMAIL_EXISTS")) {
      crearIdentidad();
      nueva = true;
      continue;
    }
    return false;
  }
  return false;
}

static void pedirVinculacion() {
  String cuerpo = String("{\"codigo\":\"") + codigo + "\",\"ts\":{\".sv\":\"timestamp\"}}";
  pedir("PUT", urlDb("/solicitudes/" + uid, true), cuerpo, nullptr);
}

// ---------- STREAM DE ÓRDENES (SSE) ----------
static bool leerLinea(String &linea, unsigned long espera) {
  linea = "";
  unsigned long t0 = millis();
  while (millis() - t0 < espera) {
    while (tlsSse.available()) {
      char c = tlsSse.read();
      if (c == '\n') { linea.trim(); return true; }
      if (linea.length() < 512) linea += c;
    }
    if (!tlsSse.connected()) return false;
    delay(5);
  }
  return false;
}

// Abre el stream; sigue redirecciones (307) de Firebase y regresa el código HTTP (200 = OK, 401 = sin vincular)
static int abrirStream() {
  String hostActual = FB_HOST;
  String rutaActual = String("/robot/ordenes.json?auth=") + idToken;

  for (int intento = 0; intento < 3; intento++) {
    tlsSse.stop();
    if (!tlsSse.connect(hostActual.c_str(), 443)) {
      avisar("No se pudo conectar a " + hostActual + ". TLS: " + errorTls(tlsSse));
      return -1;
    }
    tlsSse.print(String("GET ") + rutaActual + " HTTP/1.1\r\n"
                 "Host: " + hostActual + "\r\n"
                 "Accept: text/event-stream\r\n\r\n");

    String linea;
    if (!leerLinea(linea, 6000) || linea.length() < 12) {
      avisar("La base de datos (" + hostActual + ") no respondió al abrir las órdenes (" +
             String(tlsSse.connected() ? "sin respuesta" : "conexión cerrada") + ")");
      tlsSse.stop();
      return -2;
    }

    String estadoHttp = linea;
    int codigoHttp = estadoHttp.substring(9, 12).toInt();

    // Lee los encabezados y busca Location si hay redirección
    String locationUrl = "";
    while (leerLinea(linea, 6000) && linea.length()) {
      if (linea.startsWith("Location:") || linea.startsWith("location:")) {
        locationUrl = linea.substring(9);
        locationUrl.trim();
      }
    }

    // Redirecciones de Firebase RTDB (307 Temporary Redirect a nodo específico)
    if (codigoHttp == 307 || codigoHttp == 302 || codigoHttp == 301) {
      if (locationUrl.isEmpty()) {
        avisar("Redirección " + String(codigoHttp) + " sin encabezado Location");
        tlsSse.stop();
        return codigoHttp;
      }
      int idxProto = locationUrl.indexOf("://");
      int inicioHost = (idxProto >= 0) ? idxProto + 3 : 0;
      int idxBarra = locationUrl.indexOf('/', inicioHost);
      if (idxBarra >= 0) {
        hostActual = locationUrl.substring(inicioHost, idxBarra);
        rutaActual = locationUrl.substring(idxBarra);
      } else {
        hostActual = locationUrl.substring(inicioHost);
        rutaActual = "/";
      }
      continue;
    }

    if (codigoHttp != 200) tlsSse.stop();
    if (codigoHttp != 200 && codigoHttp != 401) {
      avisar("Al abrir las órdenes la base de datos respondió: " + estadoHttp);
    }

    lineaSse = "";
    eventoSse = "";
    tDatoSse = millis();
    return codigoHttp;
  }

  tlsSse.stop();
  avisar("Demasiadas redirecciones al abrir las órdenes");
  return -3;
}

static void programarRevision() {
  tRevision = millis() + 800;             // espera a que termine de llegar el cambio
  if (!tRevision) tRevision = 1;
}

static void revisarOrden(JsonVariant c, bool soloMarcar) {
  int64_t n = c["n"] | (int64_t)0;
  if (!n || n == ultimaOrden) return;
  ultimaOrden = n;
  if (soloMarcar) return;
  Orden o;
  strlcpy(o.cmd, c["cmd"] | "", sizeof(o.cmd));
  o.n = n;
  xQueueSend(colaOrdenes, &o, 0);
}

static void procesarEvento(const String &evento, const String &datos) {
  if (evento == "keep-alive") return;
  if (evento == "auth_revoked") { avisar("Token vencido, renovando"); tlsSse.stop(); tExpira = millis(); return; }
  if (evento == "cancel") { avisar("La base de datos cerró las órdenes (¿robot desvinculado?)"); tlsSse.stop(); return; }
  if (evento != "put" && evento != "patch") return;
  JsonDocument d;
  if (deserializeJson(d, datos)) return;
  String ruta = d["path"] | "";
  JsonVariant v = d["data"];
  if (ruta == "/") {
    if (evento == "put") {                // contenido completo (al conectar)
      if (!v["comando"].isNull()) revisarOrden(v["comando"], !ordenesVistas);
      ordenesVistas = true;
      programarRevision();
    } else {
      for (JsonPair kv : v.as<JsonObject>()) {
        String k = kv.key().c_str();
        if (k == "comando") revisarOrden(kv.value(), false);
        else if (k.startsWith("config") || k.startsWith("secretos")) programarRevision();
      }
    }
  } else if (ruta == "/comando") {
    revisarOrden(v, false);
  } else if (ruta.startsWith("/config") || ruta.startsWith("/secretos")) {
    programarRevision();
  }
}

static void leerStream() {
  while (tlsSse.available()) {
    char c = tlsSse.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineaSse.length() < 3000) lineaSse += c;
      continue;
    }
    tDatoSse = millis();
    if (lineaSse.startsWith("event:")) {
      eventoSse = lineaSse.substring(6);
      eventoSse.trim();
    } else if (lineaSse.startsWith("data:")) {
      procesarEvento(eventoSse, lineaSse.substring(5));
    }
    lineaSse = "";
  }
}

// ---------- CONFIGURACIÓN (la página manda) ----------
static void tomarTexto(JsonObject o, const char *clave_, String &destino, bool permitirVacio) {
  if (!o[clave_].is<const char *>()) return;
  String v = o[clave_].as<String>();
  if (v.isEmpty() && !permitirVacio) return;
  destino = v;
}

static void revisarConfig() {
  // 1) Sube lo que se guardó en el panel local o por BLE (solo campos con valor)
  if (configSubirPendiente()) {
    JsonDocument q;
    if (cfg.idRobot.length())   q["config/idRobot"] = cfg.idRobot;
    if (cfg.idPir.length())     q["config/idPir"] = cfg.idPir;
    if (cfg.idLdr.length())     q["config/idLdr"] = cfg.idLdr;
    if (cfg.urlApi.length())    q["config/url"] = cfg.urlApi;
    if (cfg.nombre.length())    q["config/nombre"] = cfg.nombre;
    if (cfg.appKey.length())    q["secretos/appKey"] = cfg.appKey;
    if (cfg.appSecret.length()) q["secretos/appSecret"] = cfg.appSecret;
    String cuerpo;
    serializeJson(q, cuerpo);
    if (q.size() == 0 || exito(pedir("PATCH", urlDb("/robot/ordenes", true), cuerpo, nullptr)))
      configSubirHecho();
  }

  // 2) Baja lo que se cambió en la página
  String resp;
  if (!exito(pedir("GET", urlDb("/robot/ordenes"), "", &resp))) return;
  JsonDocument d;
  if (deserializeJson(d, resp)) return;
  Config m = cfg;
  JsonObject c = d["config"], s = d["secretos"];
  if (!c.isNull()) {
    tomarTexto(c, "idRobot", m.idRobot, true);
    tomarTexto(c, "idPir", m.idPir, true);
    tomarTexto(c, "idLdr", m.idLdr, true);
    tomarTexto(c, "url", m.urlApi, true);
    tomarTexto(c, "nombre", m.nombre, false);
  }
  if (!s.isNull()) {
    tomarTexto(s, "appKey", m.appKey, false);
    tomarTexto(s, "appSecret", m.appSecret, false);
  }
  bool cambio = m.idRobot != cfg.idRobot || m.idPir != cfg.idPir || m.idLdr != cfg.idLdr ||
                m.urlApi != cfg.urlApi || m.nombre != cfg.nombre ||
                m.appKey != cfg.appKey || m.appSecret != cfg.appSecret;
  if (!cambio) return;
  String err = configValidar(m);
  if (err.length()) {
    Serial.println("[NUBE] Configuración de la página inválida: " + err);
    return;
  }
  xSemaphoreTake(candado, portMAX_DELAY);
  configNube = m;
  xSemaphoreGive(candado);
  aplicarConfig = true;                   // el loop la guarda y reinicia
}

// ---------- ENVÍOS ----------
static void enviarEstado() {
  static unsigned long tUltimoLogEstado = 0;
  static unsigned long tUltimoLogIntento = 0;
  String cuerpo;
  xSemaphoreTake(candado, portMAX_DELAY);
  if (estadoNuevo) { cuerpo = estadoJson; estadoNuevo = false; }
  xSemaphoreGive(candado);
  if (!cuerpo.length()) return;
  bool registrarIntento = millis() - tUltimoLogIntento >= 15000;
  if (registrarIntento) {
    tUltimoLogIntento = millis();
    String hora = horaLocalTexto();
    Serial.printf("[NUBE] Enviando estado | hora=%s | uptime=%lu s\n",
                  hora.c_str(), (unsigned long)(millis() / 1000));
  }
  int codigo = pedir("PUT", urlDb("/robot/estado", true), cuerpo, nullptr);
  if (registrarIntento)
    Serial.printf("[NUBE] Respuesta del estado | HTTP=%d\n", codigo);
  if (exito(codigo) && millis() - tUltimoLogEstado >= 15000) {
    tUltimoLogEstado = millis();
    String hora = horaLocalTexto();
    Serial.printf("[NUBE] Estado aceptado por Firebase | hora=%s | uptime=%lu s\n",
                  hora.c_str(), (unsigned long)(millis() / 1000));
  }
}

static void enviarRespuestas() {
  Respuesta r;
  while (xQueuePeek(colaRespuestas, &r, 0) == pdTRUE) {
    JsonDocument q;
    q["n"] = r.n;
    q["ok"] = r.ok;
    q["error"] = r.error;
    q["ts"][".sv"] = "timestamp";
    String cuerpo;
    serializeJson(q, cuerpo);
    if (!exito(pedir("PUT", urlDb("/robot/respuesta", true), cuerpo, nullptr))) return;
    xQueueReceive(colaRespuestas, &r, 0);
  }
}

static void enviarEventos() {
  EventoNube e;
  while (xQueuePeek(colaEventos, &e, 0) == pdTRUE) {
    if (!eventosNLeido) {                 // continúa el buffer circular donde se quedó
      String r;
      if (!exito(pedir("GET", urlDb("/robot/eventos_n"), "", &r))) return;
      eventosN = r.toInt();               // "null" -> 0
      eventosNLeido = true;
    }
    JsonDocument q;
    JsonObject o = q["eventos/" + String(eventosN % 20)].to<JsonObject>();
    o["tipo"] = e.tipo;
    o["valor"] = e.valor;
    o["fecha"] = e.fecha;
    o["ts"][".sv"] = "timestamp";
    q["eventos_n"] = eventosN + 1;
    String cuerpo;
    serializeJson(q, cuerpo);
    if (!exito(pedir("PATCH", urlDb("/robot", true), cuerpo, nullptr))) return;   // se reintenta luego
    eventosN++;
    xQueueReceive(colaEventos, &e, 0);
  }
}

// ---------- TAREA ----------
static void tareaNube(void *) {
  unsigned long tReintento = 0, tSolicitud = 0, tEstado = 0;
  bool yaConecto = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    if (WiFi.status() != WL_CONNECTED) {
      tlsSse.stop();
      estado = N_CONECTANDO;
      continue;
    }
    if (tReintento && (long)(millis() - tReintento) < 0) continue;
    tReintento = 0;

    // Sesión (el token dura 1 h; se renueva 5 min antes)
    if (idToken.isEmpty() || (long)(millis() - tExpira) >= 0) {
      tlsSse.stop();                      // evita solapar el TLS de autenticación con SSE
      if (!autenticar()) {
        estado = N_ERROR;
        tReintento = millis() + 30000;
        continue;
      }
      tlsSse.stop();                      // el stream se reabre con el token nuevo
    }

    // El ESP32 no tiene memoria contigua suficiente para dos sesiones TLS.
    // Pausa SSE mientras envía el estado por REST; el siguiente ciclo lo reabre.
    if (estado == N_CONECTADA && tEstado && millis() - tEstado >= 15000) {
      tlsSse.stop();
      enviarEstado();
      tEstado = millis();
      continue;
    }

    // Stream de órdenes
    if (!tlsSse.connected()) {
      int c = abrirStream();
      if (c == 200) {
        if (!yaConecto) {
          String hora = horaLocalTexto();
          Serial.printf("[NUBE] Conectada a %s | hora=%s | uptime=%lu s\n",
                        PAGINA_WEB, hora.c_str(), (unsigned long)(millis() / 1000));
          yaConecto = true;
        } else {
          Serial.printf("[NUBE] Canal SSE reabierto | uptime=%lu s\n",
                        (unsigned long)(millis() / 1000));
        }
        estado = N_CONECTADA;
        if (!tEstado) tEstado = millis();
      } else if (c == 401) {
        if (estado != N_SIN_VINCULAR)
          Serial.printf("[NUBE] Esperando vinculación desde %s · código %s\n", PAGINA_WEB, codigo);
        estado = N_SIN_VINCULAR;
        if (!tSolicitud || millis() - tSolicitud > 60000) {
          pedirVinculacion();
          tSolicitud = millis();
        }
        tReintento = millis() + 5000;
      } else {
        estado = N_ERROR;
        tReintento = millis() + 10000;
      }
      continue;
    }

    leerStream();
    if (!tlsSse.connected()) { avisar("Se cortó la conexión de órdenes. TLS: " + errorTls(tlsSse)); continue; }
    if (millis() - tDatoSse > 75000) { avisar("Sin señal de la base de datos en 75 s, reconectando"); tlsSse.stop(); continue; }
    if (tRevision && (long)(millis() - tRevision) >= 0) {
      tRevision = 0;
      tlsSse.stop();
      revisarConfig();
    }
    if (uxQueueMessagesWaiting(colaRespuestas)) tlsSse.stop();
    enviarRespuestas();
    if (uxQueueMessagesWaiting(colaEventos)) tlsSse.stop();
    enviarEventos();
  }
}

// ---------- API PARA EL .ino ----------
void nubeIniciar() {
  cargarIdentidad();
  tlsRest.useBuiltinCACertBundle();
  tlsSse.useBuiltinCACertBundle();
  colaOrdenes = xQueueCreate(5, sizeof(Orden));
  colaRespuestas = xQueueCreate(5, sizeof(Respuesta));
  colaEventos = xQueueCreate(10, sizeof(EventoNube));
  candado = xSemaphoreCreateMutex();
  candadoHttp = xSemaphoreCreateMutex();
  estado = N_CONECTANDO;
  xTaskCreatePinnedToCore(tareaNube, "nube", 16384, nullptr, 1, nullptr, 0);
  Serial.println("Página web: " PAGINA_WEB);
}

void nubeLoop() {
  if (!colaOrdenes) return;

  // Órdenes de la página -> Arduino
  Orden o;
  while (xQueueReceive(colaOrdenes, &o, 0) == pdTRUE) {
    String err = robotComando(o.cmd, "web");
    Respuesta r;
    r.n = o.n;
    r.ok = err.isEmpty();
    strlcpy(r.error, err.c_str(), sizeof(r.error));
    xQueueSend(colaRespuestas, &r, 0);
  }

  // Estado cada segundo (lo envía la tarea)
  static unsigned long tEstado = 0;
  static unsigned long tUltimoLogLatido = 0;
  if (estado == N_CONECTADA && millis() - tEstado >= 1000) {
    tEstado = millis();
    JsonDocument d;
    robotEstado(d.to<JsonObject>());
    d["nombre"] = cfg.nombre;
    d["uptime"] = millis() / 1000;
    JsonObject w = d["wifi"].to<JsonObject>();
    w["ssid"] = WiFi.SSID();
    w["rssi"] = WiFi.RSSI();
    w["ip"] = WiFi.localIP().toString();
    d["ts"][".sv"] = "timestamp";
    String s;
    serializeJson(d, s);
    xSemaphoreTake(candado, portMAX_DELAY);
    estadoJson = s;
    estadoNuevo = true;
    xSemaphoreGive(candado);
    if (millis() - tUltimoLogLatido >= 15000) {
      tUltimoLogLatido = millis();
      String hora = horaLocalTexto();
      Serial.printf("[NUBE] Latido preparado | hora=%s | uptime=%lu s\n",
                    hora.c_str(), (unsigned long)(millis() / 1000));
    }
  }

  // Configuración nueva desde la página: se guarda y reinicia
  if (aplicarConfig) {
    xSemaphoreTake(candado, portMAX_DELAY);
    Config c = configNube;
    xSemaphoreGive(candado);
    configGuardar(c, true);
    Serial.println("[NUBE] Configuración nueva desde la página web. Reiniciando...");
    delay(300);
    ESP.restart();
  }
}

void nubeEvento(const String &tipo, const String &valor, const String &fecha) {
  if (!colaEventos) return;
  EventoNube e;
  strlcpy(e.tipo, tipo.c_str(), sizeof(e.tipo));
  strlcpy(e.valor, valor.c_str(), sizeof(e.valor));
  strlcpy(e.fecha, fecha.c_str(), sizeof(e.fecha));
  xQueueSend(colaEventos, &e, 0);         // si la cola está llena, se descarta
}

String nubeEstadoTexto() {
  switch (estado) {
    case N_CONECTADA:    return "Conectada";
    case N_SIN_VINCULAR: return String("Sin vincular · código ") + codigo;
    case N_CONECTANDO:   return "Conectando…";
    case N_ERROR:        return "Error, reintentando";
    default:             return "Apagada";
  }
}
