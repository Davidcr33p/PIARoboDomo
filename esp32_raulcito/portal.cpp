// ===== Portal web: página única + API JSON =====
#include "portal.h"
#include "config.h"
#include "web_ui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include <algorithm>

static WebServer server(80);
static DNSServer dns;                 // asíncrono en el core 3.x: no necesita loop
static bool enAP = false;             // modo configuración: portal cautivo, sin PIN
static bool reiniciar = false;        // reinicio diferido (para alcanzar a responder)
static unsigned long tReinicio = 0;

// ---------- PIN: 5 intentos fallidos = 30 s de bloqueo ----------
static uint8_t fallosPin = 0;
static bool bloqueado = false;
static unsigned long tBloqueo = 0;

// ---------- EVENTOS EN RAM (buffer circular) ----------
#define MAX_EVENTOS 20
struct Evento { uint32_t seg; String fecha, tipo, valor; };
static Evento eventos[MAX_EVENTOS];
static uint32_t totalEventos = 0;

void portalEvento(const String &tipo, const String &valor, const String &fecha) {
  Evento &e = eventos[totalEventos % MAX_EVENTOS];
  e.seg = millis() / 1000;
  e.fecha = fecha;
  e.tipo = tipo;
  e.valor = valor;
  totalEventos++;
}

// ---------- UTILIDADES ----------
static void enviarJson(int codigo, JsonDocument &doc) {
  String s;
  serializeJson(doc, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(codigo, "application/json", s);
}

static void responder(int codigo, const String &error = "") {
  JsonDocument d;
  d["ok"] = error.isEmpty();
  if (error.length()) d["error"] = error;
  enviarJson(codigo, d);
}

static void programarReinicio() {
  reiniciar = true;
  tReinicio = millis();
}

// Revisa el header X-PIN (en modo AP no se pide)
static bool autorizado() {
  if (enAP) return true;
  if (bloqueado && millis() - tBloqueo < 30000) {
    responder(429, "Demasiados intentos. Espera 30 s");
    return false;
  }
  bloqueado = false;
  String pin = server.header("X-PIN");
  if (pin.isEmpty()) {
    responder(401, "PIN requerido");
    return false;
  }
  if (pin == cfg.pin) {
    fallosPin = 0;
    return true;
  }
  if (++fallosPin >= 5) {
    fallosPin = 0;
    bloqueado = true;
    tBloqueo = millis();
  }
  responder(401, "PIN incorrecto");
  return false;
}

// Portal cautivo: cualquier dominio se manda a 192.168.4.1
static bool redirigirPortal() {
  if (!enAP) return false;
  String ip = WiFi.softAPIP().toString();
  if (server.hostHeader().startsWith(ip)) return false;
  server.sendHeader("Location", "http://" + ip + "/", true);
  server.send(302, "text/plain", "");
  return true;
}

// Copia un campo del JSON; en los secretos, vacío = conservar el guardado
static void tomar(JsonDocument &in, const char *clave, String &destino, bool secreto, bool recortar = true) {
  if (!in[clave].is<const char *>()) return;
  String v = in[clave].as<String>();
  if (recortar) v.trim();
  if (secreto && v.isEmpty()) return;
  destino = v;
}

// ---------- RUTAS ----------
static void hRaiz() {
  if (redirigirPortal()) return;
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", WEB_UI);
}

static void hNoEncontrado() {
  if (redirigirPortal()) return;   // Android, iOS y Windows detectan así el portal
  responder(404, "No encontrado");
}

static void hStatus() {
  if (!autorizado()) return;
  JsonDocument d;
  d["modo"] = enAP ? "ap" : "sta";
  d["nombre"] = cfg.nombre;
  d["uptime"] = millis() / 1000;
  d["ev"] = totalEventos;
  d["pin_def"] = cfg.pin == PIN_DEFECTO;
  JsonObject w = d["wifi"].to<JsonObject>();
  if (enAP) {
    w["ap"] = WiFi.softAPSSID();
    w["ip"] = WiFi.softAPIP().toString();
    w["clientes"] = WiFi.softAPgetStationNum();
  } else {
    w["ssid"] = WiFi.SSID();
    w["rssi"] = WiFi.RSSI();
    w["ip"] = WiFi.localIP().toString();
    w["ok"] = WiFi.isConnected();
  }
  robotEstado(d.as<JsonObject>());
  enviarJson(200, d);
}

// Búsqueda asíncrona: la página consulta hasta que llega la lista
static void hScan() {
  if (!autorizado()) return;
  int n = WiFi.scanComplete();
  if (server.hasArg("nuevo") && n >= 0) {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    n = WIFI_SCAN_RUNNING;
  }
  JsonDocument d;
  if (n == WIFI_SCAN_RUNNING) {
    d["buscando"] = true;
    enviarJson(200, d);
    return;
  }
  // Ordena por señal y quita redes repetidas u ocultas
  std::vector<int> orden;
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i).length()) orden.push_back(i);
  std::sort(orden.begin(), orden.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  JsonArray redes = d["redes"].to<JsonArray>();
  for (int i : orden) {
    String ssid = WiFi.SSID(i);
    bool repetida = false;
    for (JsonObject r : redes)
      if (ssid == r["ssid"].as<const char *>()) { repetida = true; break; }
    if (repetida) continue;
    JsonObject r = redes.add<JsonObject>();
    r["ssid"] = ssid;
    r["rssi"] = WiFi.RSSI(i);
    r["segura"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    if (redes.size() >= 20) break;
  }
  WiFi.scanDelete();
  enviarJson(200, d);
}

static void hEventos() {
  if (!autorizado()) return;
  JsonDocument d;
  d["uptime"] = millis() / 1000;
  JsonArray a = d["eventos"].to<JsonArray>();
  uint32_t cant = totalEventos < MAX_EVENTOS ? totalEventos : MAX_EVENTOS;
  for (uint32_t k = 0; k < cant; k++) {          // del más nuevo al más viejo
    const Evento &e = eventos[(totalEventos - 1 - k) % MAX_EVENTOS];
    JsonObject o = a.add<JsonObject>();
    o["seg"] = e.seg;
    o["fecha"] = e.fecha;
    o["tipo"] = e.tipo;
    o["valor"] = e.valor;
  }
  enviarJson(200, d);
}

static void hCmd() {
  if (!autorizado()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) { responder(400, "JSON inválido"); return; }
  String err = robotComando(in["cmd"] | "");
  responder(err.isEmpty() ? 200 : 409, err);
}

// Nunca regresa contraseñas, APP_KEY, APP_SECRET ni PIN: solo si están guardados
static void hConfigGet() {
  if (!autorizado()) return;
  JsonDocument d;
  d["ssid"] = cfg.ssid;
  d["tiene_pass"] = cfg.pass.length() > 0;
  d["tiene_key"] = cfg.appKey.length() > 0;
  d["tiene_secret"] = cfg.appSecret.length() > 0;
  d["id_robot"] = cfg.idRobot;
  d["id_pir"] = cfg.idPir;
  d["id_ldr"] = cfg.idLdr;
  d["url"] = cfg.urlApi;
  d["nombre"] = cfg.nombre;
  d["pin_def"] = cfg.pin == PIN_DEFECTO;
  d["guardada"] = configHayWifi();
  enviarJson(200, d);
}

static void hConfigPost() {
  if (!autorizado()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) { responder(400, "JSON inválido"); return; }
  Config c = cfg;                                   // parte de lo guardado
  tomar(in, "ssid", c.ssid, false);
  tomar(in, "pass", c.pass, true, false);           // la contraseña no se recorta
  if (in["abierta"] | false) c.pass = "";           // red sin contraseña
  tomar(in, "pin", c.pin, true);
  tomar(in, "nombre", c.nombre, false);
  tomar(in, "url", c.urlApi, false);
  tomar(in, "app_key", c.appKey, true);
  tomar(in, "app_secret", c.appSecret, true);
  tomar(in, "id_robot", c.idRobot, false);
  tomar(in, "id_pir", c.idPir, false);
  tomar(in, "id_ldr", c.idLdr, false);
  if (c.nombre.isEmpty()) c.nombre = NOMBRE_DEFECTO;

  String err = configValidar(c);
  if (err.length()) { responder(400, err); return; }
  configGuardar(c);
  Serial.println("[PORTAL] Configuración guardada (red \"" + c.ssid + "\"). Reiniciando...");
  responder(200);
  programarReinicio();
}

static void hReset() {
  if (!autorizado()) return;
  configBorrar();
  Serial.println("[PORTAL] Restablecido de fábrica. Reiniciando...");
  responder(200);
  programarReinicio();
}

// ---------- INICIO / LOOP ----------
void portalIniciar(bool modoAP) {
  enAP = modoAP;
  if (enAP) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());   // todo dominio apunta al ESP32
  }
  server.on("/", HTTP_GET, hRaiz);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/scan", HTTP_GET, hScan);
  server.on("/api/events", HTTP_GET, hEventos);
  server.on("/api/cmd", HTTP_POST, hCmd);
  server.on("/api/config", HTTP_GET, hConfigGet);
  server.on("/api/config", HTTP_POST, hConfigPost);
  server.on("/api/reset", HTTP_POST, hReset);
  server.onNotFound(hNoEncontrado);
  const char *headers[] = {"X-PIN"};
  server.collectHeaders(headers, 1);
  server.begin();
}

void portalLoop() {
  server.handleClient();
  if (reiniciar && millis() - tReinicio > 1500) ESP.restart();
}
