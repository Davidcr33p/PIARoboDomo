// ===== Raulcito bracito - ARDUINO UNO (brazo 4 GDL + tapa del baúl) =====
// Recibe órdenes del ESP32 por serial y mueve servos + motor DC (L298N).
#include <Servo.h>
#include <SoftwareSerial.h>

// --- Comunicación con ESP32 ---
SoftwareSerial esp(7, 8);           // RX=7 (<- TX2 ESP32), TX=8 (-> RX2 ESP32)

// --- Servos (según diagrama del reporte) ---
const byte P_BASE = 3, P_HOMBRO = 5, P_CODO = 6, P_PINZA = 9;
Servo base, hombro, codo, pinza;
Servo* srv[3] = {&base, &hombro, &codo};

// --- Puente H L298N (motor DC de la tapa) ---
const byte ENA = 11, IN1 = 12, IN2 = 13;
const byte VEL_TAPA = 200;          // 0-255
const unsigned long T_TAPA = 1500;  // ms que gira el motor para abrir/cerrar

// --- Pinza ---
const int PINZA_ABIERTA = 90, PINZA_CERRADA = 20;

// --- Position List (copiar ángulos validados en Cosimir): {base, hombro, codo} ---
const int REPOSO[3]     = {90,  90,  90};
const int SUELO[3]      = {90, 140,  40};
const int SOBRE_BAUL[3] = { 0,  80, 100};

int actual[3] = {90, 90, 90};
String linea = "";

// Mueve las 3 articulaciones de forma suave y simultánea
void mover(const int p[3]) {
  bool listo = false;
  while (!listo) {
    listo = true;
    for (byte i = 0; i < 3; i++) {
      if (actual[i] != p[i]) {
        actual[i] += (actual[i] < p[i]) ? 1 : -1;
        srv[i]->write(actual[i]);
        listo = false;
      }
    }
    delay(15);                       // velocidad del brazo
  }
}

void setPinza(int ang) { pinza.write(ang); delay(400); }

void tapa(bool abrir) {
  digitalWrite(IN1, abrir);
  digitalWrite(IN2, !abrir);
  analogWrite(ENA, VEL_TAPA);
  delay(T_TAPA);
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

// Secuencia completa: tomar juguete del suelo y guardarlo en el baúl
void recoger() {
  esp.println("EST:RECOGIENDO");
  setPinza(PINZA_ABIERTA);
  mover(SUELO);
  setPinza(PINZA_CERRADA);
  mover(REPOSO);
  tapa(true);
  esp.println("EST:TAPA_ABIERTA");
  mover(SOBRE_BAUL);
  setPinza(PINZA_ABIERTA);
  mover(REPOSO);
  tapa(false);
  esp.println("OK:RECOGIDO");
}

void procesar(String cmd) {
  cmd.trim();
  if (cmd == "RECOGER")      recoger();
  else if (cmd == "REPOSO") { mover(REPOSO); setPinza(PINZA_ABIERTA); esp.println("OK:REPOSO"); }
  else if (cmd == "PING")    esp.println("OK:PONG");
  else if (cmd.length())     esp.println("ERR:" + cmd);
}

void setup() {
  Serial.begin(9600);                // monitor / Virtual Terminal
  esp.begin(9600);

  base.attach(P_BASE);
  hombro.attach(P_HOMBRO);
  codo.attach(P_CODO);
  pinza.attach(P_PINZA);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  for (byte i = 0; i < 3; i++) srv[i]->write(actual[i]);
  setPinza(PINZA_ABIERTA);
  esp.println("OK:LISTO");
  Serial.println("Arduino listo");
}

void loop() {
  while (esp.available()) {
    char c = esp.read();
    if (c == '\n') {
      Serial.println("CMD: " + linea);
      procesar(linea);
      linea = "";
    } else if (c != '\r') {
      linea += c;
    }
  }
}
