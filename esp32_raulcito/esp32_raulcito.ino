// ===== Raulcito bracito - ESP32 (cerebro: sensores + Alexa + web + Arduino) =====
// Librería requerida: SinricPro (Gestor de librerías del Arduino IDE)
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <SinricProMotionsensor.h>
#include <SinricProContactsensor.h>

// ---------- CONFIGURACIÓN ----------
#define WIFI_SSID   "TU_WIFI"
#define WIFI_PASS   "TU_PASSWORD"

#define APP_KEY     "APP_KEY_SINRICPRO"
#define APP_SECRET  "APP_SECRET_SINRICPRO"
#define ID_ROBOT    "ID_DISPOSITIVO_SWITCH"     // Switch  "Raulcito"   (inicia limpieza)
#define ID_PIR      "ID_DISPOSITIVO_MOTION"     // Motion  "Movimiento" (alarma nocturna)
#define ID_LDR      "ID_DISPOSITIVO_CONTACT"    // Contact "Oscuridad"  (encender focos)

#define URL_API     "http://TU_SERVIDOR/raulcito/guardar.php"
#define ROBOT_NOMBRE "raulcito-01"              // identifica cada robot en la BD

// ---------- PINES ----------
#define TRIG 5
#define ECHO 18
#define PIR  4
#define LDR  36
#define RX2  16     // <- pin 8 Arduino
#define TX2  17     // -> pin 7 Arduino

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

// ---------- UTILIDADES ----------
String fechaHora() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return "";
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

bool esNoche() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return false;
  return t.tm_hour >= NOCHE_INICIO || t.tm_hour < NOCHE_FIN;
}

// Guarda un evento en la base de datos SQL (app web)
void registrar(String tipo, String valor) {
  Serial.println("[LOG] " + tipo + " = " + valor);
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(URL_API);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  String json = "{\"robot\":\"" ROBOT_NOMBRE "\",\"tipo\":\"" + tipo +
                "\",\"valor\":\"" + valor + "\",\"fecha\":\"" + fechaHora() + "\"}";
  http.POST(json);
  http.end();
}

float distancia() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 30000);
  return us ? us * 0.0343 / 2 : -1;
}

// ---------- ALEXA ----------
// Rutina de Alexa "limpia el cuarto" -> enciende el switch "Raulcito"
bool onPowerState(const String &id, bool &state) {
  limpiando = state;
  registrar("alexa", state ? "limpieza_inicio" : "limpieza_fin");
  if (!state) { Serial2.println("REPOSO"); esperandoBrazo = false; }
  return true;
}

// ---------- RESPUESTAS DEL ARDUINO ----------
void leerArduino() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      linea.trim();
      Serial.println("[ARDUINO] " + linea);
      if (linea == "OK:RECOGIDO") {
        esperandoBrazo = false;
        registrar("brazo", "juguete_guardado");
      } else if (linea == "EST:TAPA_ABIERTA") {
        registrar("tapa", "abierta");
      } else if (linea.startsWith("ERR")) {
        registrar("error", linea);
      }
      linea = "";
    } else if (c != '\r') linea += c;
  }
}

// ---------- SENSORES ----------
void revisarUltrasonico() {
  if (!limpiando || esperandoBrazo || millis() - tUltra < 300) return;
  tUltra = millis();
  float d = distancia();
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
  bool nuevo = oscuro ? (luz < UMBRAL_OSCURO + HISTERESIS) : (luz < UMBRAL_OSCURO - HISTERESIS);
  if (nuevo != oscuro) {
    oscuro = nuevo;
    SinricProContactsensor &ldr = SinricPro[ID_LDR];
    ldr.sendContactEvent(oscuro);              // cerrado = oscuro -> rutina enciende focos
    registrar("ldr", oscuro ? "oscuro" : "claro");
  }
}

void revisarPIR() {
  bool mov = digitalRead(PIR);
  if (mov == movPrev) return;
  movPrev = mov;
  if (!esNoche()) return;                      // seguridad solo de noche
  SinricProMotionsensor &pir = SinricPro[ID_PIR];
  pir.sendMotionEvent(mov);                    // rutina: alarma, luz roja, notificación
  if (mov) registrar("pir", "intrusion");
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(PIR, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWi-Fi OK: " + WiFi.localIP().toString());

  configTzTime("CST6", "pool.ntp.org");        // hora de Monterrey

  SinricProSwitch &robot = SinricPro[ID_ROBOT];
  robot.onPowerState(onPowerState);
  SinricPro[ID_PIR];
  SinricPro[ID_LDR];
  SinricPro.onConnected([] { Serial.println("SinricPro conectado"); });
  SinricPro.begin(APP_KEY, APP_SECRET);

  registrar("sistema", "encendido");
}

void loop() {
  SinricPro.handle();
  leerArduino();
  revisarUltrasonico();
  revisarLDR();
  revisarPIR();

  if (esperandoBrazo && millis() - tBrazo > TIMEOUT_BRAZO) {
    esperandoBrazo = false;
    registrar("error", "brazo_sin_respuesta");
  }
}
