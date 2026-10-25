// ===== Raulcito bracito - ESP32 (cerebro: sensores + Alexa + web + Arduino) =====
// Placa: "ESP32 Dev Module" · Partición: "Huge APP (3MB No OTA/1MB SPIFFS)"
// Librerías: SinricPro, ArduinoJson 7, NimBLE-Arduino (versiones en README.md)
// Wi-Fi, claves de SinricPro, URL de la API y PIN se capturan en el portal web
// o por Bluetooth y se guardan en la NVS: ya no hay credenciales en el código.
// Ya conectado, también se controla desde internet: https://raulcito-bracito.web.app
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#define SINRICPRO_NOMDNS               // el mDNS queda libre para raulcito.local
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <SinricProMotionsensor.h>
#include <SinricProContactsensor.h>
#include "config.h"
#include "portal.h"
#include "ble_setup.h"
#include "nube.h"

// ---------- MODO CONFIGURACIÓN ----------
#define AP_SSID   "Raulcito-Setup"
#define AP_PASS   "raulcito123"        // mínimo 8 caracteres
#define HOSTNAME  "raulcito"           // panel en http://raulcito.local
const unsigned long WIFI_TIMEOUT = 20000;    // ms sin Wi-Fi antes de ir a modo AP
const unsigned long AP_REINTENTO = 300000;   // ms en AP sin uso (con red guardada) antes de reintentar
const unsigned long BOOT_RESET   = 5000;     // ms presionando BOOT para borrar la configuración

// ---------- PINES ----------
#define TRIG 5
#define ECHO 18
#define PIR  4
#define LDR  36
#define RX_ARD 16   // <- pin 8 Arduino  (el core ya usa los nombres RX2/TX2)
#define TX_ARD 17   // -> pin 7 Arduino
#define BOOT_BTN   0   // botón BOOT de la placa
#define LED_ESTADO 2   // LED azul de la placa

// ---------- PARÁMETROS ----------
const float DIST_JUGUETE   = 15.0;   // cm para detectar juguete
const int   UMBRAL_OSCURO  = 1500;   // 0-4095 (menor = más oscuro); calibrar
const int   HISTERESIS     = 150;
const int   NOCHE_INICIO   = 22;     // h
const int   NOCHE_FIN      = 6;      // h
const unsigned long TIMEOUT_BRAZO = 30000;

// ---------- ESTADO ----------
bool limpiando = false, esperandoBrazo = false, oscuro = false, movPrev = false;
unsigned long tUltra = 0, tLdr = 0, tBrazo = 0;
String linea = "";

// SIN_CONFIG -> (AP/BLE) -> CONECTANDO -> CONECTADO -> (falla) -> AP
enum Modo { SIN_CONFIG, CONECTANDO, CONECTADO, MODO_AP };
Modo modo = SIN_CONFIG;
bool sinricActivo = false, usaRobot = false, usaPir = false, usaLdr = false;
float ultimaDist = -1;
int ultimaLuz = 0;
String ultimoArduino = "";
unsigned long tArduino = 0, tSinWifi = 0, tApUso = 0, tBoot = 0;

// Eventos que la tarea HTTP manda a guardar.php
struct EventoHttp { char tipo[24]; char valor[64]; char fecha[20]; };
QueueHandle_t colaHttp = nullptr;

// ---------- UTILIDADES ----------
// Hora local sin esperar; false si todavía no hay hora de internet (NTP)
bool horaLocal(tm &t) {
  time_t ahora = time(nullptr);
  if (ahora < 1700000000) return false;
  localtime_r(&ahora, &t);
  return true;
}

String fechaHora() {
  tm t;
  if (!horaLocal(t)) return "";
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

bool esNoche() {
  tm t;
  if (!horaLocal(t)) return false;
  return t.tm_hour >= NOCHE_INICIO || t.tm_hour < NOCHE_FIN;
}

// Guarda un evento: buffer local (panel), página de internet y base de datos SQL.
// No bloquea: los envíos los hacen tareas aparte. Sin Wi-Fi solo queda en el buffer.
void registrar(const String &tipo, const String &valor) {
  Serial.println("[LOG] " + tipo + " = " + valor);
  String fecha = fechaHora();
  portalEvento(tipo, valor, fecha);
  nubeEvento(tipo, valor, fecha);
  if (!colaHttp || WiFi.status() != WL_CONNECTED) return;
  EventoHttp e;
  strlcpy(e.tipo, tipo.c_str(), sizeof(e.tipo));
  strlcpy(e.valor, valor.c_str(), sizeof(e.valor));
  strlcpy(e.fecha, fecha.c_str(), sizeof(e.fecha));
  xQueueSend(colaHttp, &e, 0);         // si la cola está llena, se descarta
}

// Tarea aparte: el loop nunca espera al servidor (timeout de 3 s por envío)
void tareaHttp(void *) {
  EventoHttp e;
  for (;;) {
    if (xQueueReceive(colaHttp, &e, portMAX_DELAY) != pdTRUE) continue;
    if (WiFi.status() != WL_CONNECTED || cfg.urlApi.isEmpty()) continue;
    JsonDocument doc;
    doc["robot"] = cfg.nombre;
    doc["tipo"]  = e.tipo;
    doc["valor"] = e.valor;
    doc["fecha"] = e.fecha;
    String json;
    serializeJson(doc, json);
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    if (!http.begin(cfg.urlApi)) continue;
    http.addHeader("Content-Type", "application/json");
    int codigo = http.POST(json);
    if (codigo <= 0) Serial.println("[HTTP] Error al enviar: " + http.errorToString(codigo));
    http.end();
  }
}

float distancia() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 30000);
  return us ? us * 0.0343 / 2 : -1;
}

// ---------- ALEXA ----------
// Activa o detiene el modo limpieza (lo usan Alexa y el panel web)
void cambiarLimpieza(bool on, const char *origen) {
  limpiando = on;
  registrar(origen, on ? "limpieza_inicio" : "limpieza_fin");
  if (!on) { Serial2.println("REPOSO"); esperandoBrazo = false; }
}

// Rutina de Alexa "limpia el cuarto" -> enciende el switch "Raulcito"
bool onPowerState(const String &, bool &state) {
  cambiarLimpieza(state, "alexa");
  return true;
}

// Registra los dispositivos con ID válido y conecta (solo con claves válidas,
// así SinricPro nunca imprime el APP_SECRET en el monitor serie)
void iniciarSinric() {
  if (!configSinricOk(cfg)) {
    Serial.println("SinricPro: sin claves válidas, Alexa desactivada");
    return;
  }
  usaRobot = formatoDeviceId(cfg.idRobot);
  usaPir   = formatoDeviceId(cfg.idPir);
  usaLdr   = formatoDeviceId(cfg.idLdr);
  if (!usaRobot && !usaPir && !usaLdr) {
    Serial.println("SinricPro: sin Device IDs válidos, Alexa desactivada");
    return;
  }
  if (usaRobot) {
    SinricProSwitch &robot = SinricPro[cfg.idRobot];
    robot.onPowerState(onPowerState);
  }
  if (usaPir) { SinricProMotionsensor &pir = SinricPro[cfg.idPir]; (void)pir; }
  if (usaLdr) { SinricProContactsensor &ldr = SinricPro[cfg.idLdr]; (void)ldr; }
  SinricPro.onConnected([] { Serial.println("SinricPro conectado"); });
  SinricPro.onDisconnected([] { Serial.println("SinricPro desconectado"); });
  SinricPro.begin(cfg.appKey, cfg.appSecret);
  sinricActivo = true;
}

// ---------- PANEL WEB (lo llama portal.cpp) ----------
void robotEstado(JsonObject o) {
  o["dist"] = ultimaDist < 0 ? -1.0f : roundf(ultimaDist * 10) / 10;
  o["pir"] = digitalRead(PIR) == HIGH;
  o["luz"] = ultimaLuz;
  o["oscuro"] = oscuro;
  o["limpiando"] = limpiando;
  o["brazo"] = esperandoBrazo;
  o["arduino"] = ultimoArduino;
  o["arduino_seg"] = tArduino ? (long)((millis() - tArduino) / 1000) : -1L;
  o["sinric_cfg"] = sinricActivo;
  o["sinric"] = sinricActivo && SinricPro.isConnected();
  o["hora"] = fechaHora();
  o["nube"] = nubeEstadoTexto();
}

// origen: "panel" (raulcito.local) o "web" (página de internet)
String robotComando(const String &cmd, const char *origen) {
  if (cmd == "LIMPIEZA_ON" || cmd == "LIMPIEZA_OFF") {
    bool on = cmd == "LIMPIEZA_ON";
    cambiarLimpieza(on, origen);
    if (sinricActivo && usaRobot) {          // Alexa queda sincronizada
      SinricProSwitch &robot = SinricPro[cfg.idRobot];
      robot.sendPowerStateEvent(on);
    }
    return "";
  }
  if (cmd == "RECOGER") {
    if (esperandoBrazo) return "El brazo está ocupado";
    registrar(origen, "recoger");
    Serial2.println("RECOGER");
    esperandoBrazo = true;
    tBrazo = millis();
    return "";
  }
  if (cmd == "REPOSO") {
    registrar(origen, "reposo");
    Serial2.println("REPOSO");
    esperandoBrazo = false;
    return "";
  }
  if (cmd == "PING") {
    Serial2.println("PING");                 // la respuesta OK:PONG aparece en el panel
    return "";
  }
  return "Comando desconocido";
}

// ---------- RESPUESTAS DEL ARDUINO ----------
void leerArduino() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      linea.trim();
      Serial.println("[ARDUINO] " + linea);
      if (linea.length()) { ultimoArduino = linea; tArduino = millis(); }
      if (linea == "OK:RECOGIDO") {
        esperandoBrazo = false;
        registrar("brazo", "juguete_guardado");
      } else if (linea == "EST:TAPA_ABIERTA") {
        registrar("tapa", "abierta");
      } else if (linea.startsWith("ERR")) {
        registrar("error", linea);
      }
      linea = "";
    } else if (c != '\r') {
      linea += c;
      if (linea.length() > 64) linea = "";   // basura sin fin de línea
    }
  }
}

// ---------- SENSORES ----------
void revisarUltrasonico() {
  if (millis() - tUltra < 300) return;
  tUltra = millis();
  ultimaDist = distancia();                  // se mide siempre para el panel
  if (!limpiando || esperandoBrazo) return;
  float d = ultimaDist;
  if (d > 0 && d < DIST_JUGUETE) {
    registrar("ultrasonico", String(d, 1) + " cm");
    Serial2.println("RECOGER");
    esperandoBrazo = true;
    tBrazo = millis();
  }
}

void revisarLDR() {
  if (millis() - tLdr < 1000) return;
  tLdr = millis();
  int luz = analogRead(LDR);
  ultimaLuz = luz;
  bool nuevo = oscuro ? (luz < UMBRAL_OSCURO + HISTERESIS) : (luz < UMBRAL_OSCURO - HISTERESIS);
  if (nuevo != oscuro) {
    oscuro = nuevo;
    if (sinricActivo && usaLdr) {
      SinricProContactsensor &ldr = SinricPro[cfg.idLdr];
      ldr.sendContactEvent(oscuro);          // cerrado = oscuro -> rutina enciende focos
    }
    registrar("ldr", oscuro ? "oscuro" : "claro");
  }
}

void revisarPIR() {
  bool mov = digitalRead(PIR);
  if (mov == movPrev) return;
  movPrev = mov;
  if (!esNoche()) return;                    // seguridad solo de noche
  if (sinricActivo && usaPir) {
    SinricProMotionsensor &pir = SinricPro[cfg.idPir];
    pir.sendMotionEvent(mov);                // rutina: alarma, luz roja, notificación
  }
  if (mov) registrar("pir", "intrusion");
}

// ---------- BOTÓN BOOT Y LED ----------
// BOOT presionado 5 s: borra la configuración y vuelve al modo AP
void revisarBoton() {
  if (digitalRead(BOOT_BTN) == HIGH) { tBoot = 0; return; }
  if (!tBoot) { tBoot = millis(); return; }
  if (millis() - tBoot < BOOT_RESET) return;
  configBorrar();
  Serial.println("BOOT 5 s: configuración borrada. Suelta el botón para reiniciar.");
  while (digitalRead(BOOT_BTN) == LOW) {     // con BOOT presionado arrancaría en modo programación
    digitalWrite(LED_ESTADO, (millis() / 80) % 2);
    delay(10);
  }
  ESP.restart();
}

// Conectando: parpadeo rápido · Modo AP: parpadeo lento · Conectado: fijo
void actualizarLed() {
  bool on = false;
  if (modo == CONECTANDO) on = (millis() / 100) % 2;
  else if (modo == MODO_AP) on = (millis() / 500) % 2;
  else if (modo == CONECTADO) on = WiFi.status() == WL_CONNECTED;
  digitalWrite(LED_ESTADO, on);
}

// ---------- CONEXIÓN ----------
// Intenta conectarse a la red guardada durante WIFI_TIMEOUT
bool conectarWifi() {
  modo = CONECTANDO;
  Serial.print("Conectando a \"" + cfg.ssid + "\"");
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.length() ? cfg.pass.c_str() : nullptr);
  unsigned long t0 = millis(), tPunto = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT) {
    revisarBoton();
    actualizarLed();
    if (millis() - tPunto > 500) { tPunto = millis(); Serial.print("."); }
    delay(20);
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void modoNormal() {
  modo = CONECTADO;
  WiFi.setSleep(false);                      // respuesta más rápida (sin BLE en este modo)
  WiFi.setAutoReconnect(true);
  String ip = WiFi.localIP().toString();
  Serial.println("Wi-Fi OK: " + ip + "  (red \"" + WiFi.SSID() + "\")");
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  Serial.println("Panel: http://" HOSTNAME ".local  o  http://" + ip);
  if (cfg.pin == PIN_DEFECTO) Serial.println("Aviso: el panel usa el PIN por defecto (1234); cámbialo en Configuración");

  configTzTime("CST6", "pool.ntp.org");      // hora de Monterrey

  colaHttp = xQueueCreate(10, sizeof(EventoHttp));
  xTaskCreatePinnedToCore(tareaHttp, "http", 8192, nullptr, 1, nullptr, 0);

  portalIniciar(false);
  nubeIniciar();
  iniciarSinric();
  registrar("sistema", "encendido");
}

void modoConfig() {
  modo = MODO_AP;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);                    // AP + STA para poder escanear redes
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  portalIniciar(true);
  bleIniciar();
  Serial.println("Modo configuración:");
  Serial.println("  Wi-Fi: conéctate a la red \"" AP_SSID "\" y abre http://" + WiFi.softAPIP().toString());
  Serial.println("  Bluetooth: \"Raulcito-BLE\" con la app nRF Connect");
  registrar("sistema", configHayWifi() ? "wifi_fallo_modo_config" : "modo_configuracion");
  tApUso = millis();
}

// Conectado: si el Wi-Fi se pierde 20 s, reinicia (y si sigue sin red, entra a modo AP)
void revisarWifi() {
  if (WiFi.status() == WL_CONNECTED) { tSinWifi = 0; return; }
  if (!tSinWifi) {
    tSinWifi = millis();
    Serial.println("Wi-Fi perdido, reintentando...");
    return;
  }
  if (millis() - tSinWifi > WIFI_TIMEOUT) {
    Serial.println("Sin Wi-Fi por 20 s: reiniciando");
    delay(100);
    ESP.restart();
  }
}

// En AP con una red guardada: si nadie usa el portal ni BLE en 5 min, reintenta el Wi-Fi
void revisarReintentoAP() {
  static unsigned long tRevision = 0;
  if (!configHayWifi() || millis() - tRevision < 1000) return;
  tRevision = millis();
  if (WiFi.softAPgetStationNum() > 0 || bleOcupado()) { tApUso = millis(); return; }
  if (millis() - tApUso > AP_REINTENTO) {
    Serial.println("Portal sin uso por 5 min: reintentando la red guardada");
    delay(100);
    ESP.restart();
  }
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_ARD, TX_ARD);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(PIR, INPUT);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  pinMode(LED_ESTADO, OUTPUT);

  Serial.println("\n===== Raulcito bracito =====");
  configCargar();
  WiFi.persistent(false);                    // las claves viven en nuestra NVS, no en la del Wi-Fi

  if (configHayWifi() && conectarWifi()) modoNormal();
  else {
    if (configHayWifi()) Serial.println("No se pudo conectar en 20 s");
    else Serial.println("Sin configuración guardada");
    modoConfig();
  }
}

void loop() {
  revisarBoton();
  actualizarLed();
  portalLoop();
  if (modo == MODO_AP) {
    bleLoop();
    revisarReintentoAP();
  } else {
    if (sinricActivo) SinricPro.handle();
    nubeLoop();
    revisarWifi();
  }
  leerArduino();
  revisarUltrasonico();
  revisarLDR();
  revisarPIR();

  if (esperandoBrazo && millis() - tBrazo > TIMEOUT_BRAZO) {
    esperandoBrazo = false;
    registrar("error", "brazo_sin_respuesta");
  }
}
