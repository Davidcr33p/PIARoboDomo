// ===== Raulcito bracito - ARDUINO UNO (Brazo robótico 4 GDL + tapa DC + sensores) =====
// Hardware en tiempo real:
//   - 4 Servomotores principales: Base (pin 3), Hombro (pin 5), Codo (pin 6), Pinza (pin 9)
//   - Tapa del baúl: Motor DC con puente H (IN1=pin 11, IN2=pin 12, ENA a 5V fijo)
//   - Sensores a 5V nativos: Ultrasónico (HC-SR04: Trig 2, Echo 4), PIR (pin 10), LDR (pin A0)
//   - Comunicación con ESP32: SoftwareSerial (RX=7, TX=8) a 9600 baud

#include <Servo.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// --- Comunicación con ESP32 ---
SoftwareSerial esp(7, 8);           // RX=7 (<- TX2 ESP32), TX=8 (-> RX2 ESP32)

// --- Servomotores del brazo robótico ---
const byte P_BASE   = 3;
const byte P_HOMBRO = 5;
const byte P_CODO   = 6;
const byte P_PINZA  = 9;

Servo base, hombro, codo, pinza;
Servo* srv[3] = {&base, &hombro, &codo};

// --- Tapa del baúl: Motor DC con puente H ---
// ENA conectado directo a 5V (velocidad máxima fija)
const byte TAPA_IN1 = 11;          // HIGH=abrir
const byte TAPA_IN2 = 12;          // HIGH=cerrar
const unsigned long TAPA_MS = 600; // ms que tarda en abrir/cerrar

// --- Paro físico: pulsador normalmente abierto entre A1 y GND ---
const byte BOTON_PARO = A1;
bool paroFisicoActivo = false;
bool paroSolicitado = false;

// --- Sensores (alimentados a 5V nativos del Arduino Uno) ---
const byte TRIG = 2;               // HC-SR04 Trigger
const byte ECHO = 4;               // HC-SR04 Echo
const byte PIR  = 10;              // Sensor de movimiento PIR
const byte LDR  = A0;              // Sensor de luz LDR (analógico 0-1023)

// --- Rango inicial de la pinza: ajustable desde la página ---
const int PINZA_ABIERTA = 90, PINZA_CERRADA = 20;
bool pinzaCerrada = false;
bool tapaAbierta  = false;

// --- Posiciones del brazo {base, hombro, codo} ---
const int REPOSO[3]     = {90,  90,  90};
const int SUELO[3]      = {90, 140,  40};
const int SOBRE_BAUL[3] = { 0,  80, 100};

int actual[4] = {90, 90, 90, PINZA_ABIERTA};
String linea = "";
String comandoEnCola = "";

const byte EEPROM_MARCA_DIR = 8;
const byte EEPROM_MARCA = 0xA7;

void guardarPosiciones() {
  EEPROM.update(EEPROM_MARCA_DIR, EEPROM_MARCA);
  for (byte i = 0; i < 4; i++) EEPROM.update(i, (byte)actual[i]);
}

void cargarPosiciones() {
  if (EEPROM.read(EEPROM_MARCA_DIR) != EEPROM_MARCA) return;
  const int minimos[4] = {0, 30, 20, PINZA_CERRADA};
  const int maximos[4] = {180, 160, 160, PINZA_ABIERTA};
  for (byte i = 0; i < 4; i++) {
    byte valor = EEPROM.read(i);
    if (valor < minimos[i] || valor > maximos[i]) return;
    actual[i] = valor;
  }
  pinzaCerrada = actual[3] <= PINZA_CERRADA;
}

// Activa cada servo solo al recibir una orden; evita movimientos al arrancar.
void escribirServo(Servo &servo, byte pin, int angulo) {
  if (!servo.attached()) servo.attach(pin);
  servo.write(angulo);
}

// Telemetría
unsigned long tSensores   = 0;
unsigned long tDiagnostico = 0;
float ultimaDist = -1;
int   ultimaPir  = 0;
int   ultimaLuz  = 0;

// =========================================================
// DIAGNÓSTICO - imprime una línea en el monitor serie
// =========================================================
void imprimirDiagnostico() {
  Serial.println(F("-------- ESTADO --------"));

  // Sensores
  Serial.print(F("  Distancia (HC-SR04): "));
  if (ultimaDist < 0) Serial.println(F("sin lectura / fuera de rango"));
  else { Serial.print(ultimaDist, 1); Serial.println(F(" cm")); }

  Serial.print(F("  PIR (movimiento)   : "));
  Serial.println(ultimaPir ? F("DETECTADO") : F("sin movimiento"));

  Serial.print(F("  LDR (luz)          : "));
  Serial.print(ultimaLuz);
  Serial.println(ultimaLuz < 400 ? F("  <- OSCURO") : F("  <- claro"));

  // Servomotores
  Serial.print(F("  Base    (pin 3) : ")); Serial.print(actual[0]); Serial.println(F("°"));
  Serial.print(F("  Hombro  (pin 5) : ")); Serial.print(actual[1]); Serial.println(F("°"));
  Serial.print(F("  Codo    (pin 6) : ")); Serial.print(actual[2]); Serial.println(F("°"));
  Serial.print(F("  Pinza   (pin 9) : ")); Serial.print(actual[3]); Serial.println(F("°"));

  // Tapa DC
  Serial.print(F("  Tapa DC (pin11/12): "));
  Serial.println(tapaAbierta ? F("ABIERTA") : F("cerrada"));

  // Comunicación con ESP32
  Serial.println(F("  ESP32 (SoftSerial): TX=pin8, RX=pin7, 9600 baud"));
  Serial.println(F("------------------------"));
}

// =========================================================
// MOVIMIENTO
// =========================================================
void revisarComandosDuranteMovimiento() {
  while (esp.available()) {
    char c = esp.read();
    if (c == '\n') {
      String cmd = linea;
      cmd.trim();
      linea = "";
      if (cmd == "STOP") {
        paroSolicitado = true;
        comandoEnCola = "";
        digitalWrite(TAPA_IN1, LOW);
        digitalWrite(TAPA_IN2, LOW);
      } else if (cmd.length() > 0) {
        // Conserva el siguiente comando para procesarlo al terminar la secuencia.
        if (comandoEnCola.length() == 0) comandoEnCola = cmd;
      }
    } else if (c != '\r') {
      linea += c;
    }
  }
}

bool comprobarParo() {
  revisarComandosDuranteMovimiento();
  if (digitalRead(BOTON_PARO) != LOW && !paroSolicitado) return false;
  paroSolicitado = false;
  comandoEnCola = "";
  digitalWrite(TAPA_IN1, LOW);
  digitalWrite(TAPA_IN2, LOW);
  if (base.attached()) base.write(actual[0]);
  if (hombro.attached()) hombro.write(actual[1]);
  if (codo.attached()) codo.write(actual[2]);
  if (pinza.attached()) pinza.write(actual[3]);
  guardarPosiciones();
  esp.println("OK:PARADO");
  Serial.println(F("[PARO] Movimiento interrumpido; servos mantienen el ultimo angulo"));
  return true;
}

bool moverEje(Servo &servo, byte pin, byte indice, int objetivo) {
  if (!servo.attached()) {
    servo.attach(pin);
    servo.write(actual[indice]);
    delay(40);
  }
  objetivo = constrain(objetivo, indice == 1 ? 30 : indice == 2 ? 20 : indice == 3 ? PINZA_CERRADA : 0,
                       indice == 1 || indice == 2 ? 160 : indice == 3 ? PINZA_ABIERTA : 180);
  while (actual[indice] != objetivo) {
    if (comprobarParo()) return false;
    actual[indice] += (actual[indice] < objetivo) ? 1 : -1;
    servo.write(actual[indice]);
    delay(18);
  }
  guardarPosiciones();
  return true;
}

bool mover(const int p[3]) {
  Serial.print(F("[SERVO] Moviendo -> Base:"));
  Serial.print(p[0]); Serial.print(F(" Hombro:"));
  Serial.print(p[1]); Serial.print(F(" Codo:"));
  Serial.println(p[2]);
  bool listo = false;
  for (byte i = 0; i < 3; i++) {
    if (!srv[i]->attached()) {
      byte pin = i == 0 ? P_BASE : i == 1 ? P_HOMBRO : P_CODO;
      srv[i]->attach(pin);
      srv[i]->write(actual[i]);
    }
  }
  delay(40);
  while (!listo) {
    if (comprobarParo()) return false;
    listo = true;
    for (byte i = 0; i < 3; i++) {
      if (actual[i] != p[i]) {
        actual[i] += (actual[i] < p[i]) ? 1 : -1;
        escribirServo(*srv[i], i == 0 ? P_BASE : i == 1 ? P_HOMBRO : P_CODO, actual[i]);
        listo = false;
      } else if (!srv[i]->attached()) {
        // Un primer comando como REPOSO también debe fijar la posición objetivo.
        escribirServo(*srv[i], i == 0 ? P_BASE : i == 1 ? P_HOMBRO : P_CODO, p[i]);
      }
    }
    delay(15);
  }
  guardarPosiciones();
  Serial.println(F("[SERVO] Posicion alcanzada"));
  return true;
}

bool setPinza(int ang) {
  Serial.print(F("[SERVO] Pinza -> "));
  Serial.print(ang); Serial.println(F("°"));
  bool ok = moverEje(pinza, P_PINZA, 3, ang);
  if (ok) pinzaCerrada = (actual[3] <= PINZA_CERRADA);
  return ok;
}

bool setTapa(bool abrir) {
  Serial.print(F("[TAPA DC] "));
  Serial.println(abrir ? F("Abriendo...") : F("Cerrando..."));
  if (abrir) {
    digitalWrite(TAPA_IN1, HIGH);
    digitalWrite(TAPA_IN2, LOW);
  } else {
    digitalWrite(TAPA_IN1, LOW);
    digitalWrite(TAPA_IN2, HIGH);
  }
  unsigned long inicio = millis();
  while (millis() - inicio < TAPA_MS) {
    if (comprobarParo()) {
      digitalWrite(TAPA_IN1, LOW);
      digitalWrite(TAPA_IN2, LOW);
      Serial.println(F("[PARO] Tapa detenida"));
      return false;
    }
    delay(5);
  }
  digitalWrite(TAPA_IN1, LOW);   // freno (rueda libre)
  digitalWrite(TAPA_IN2, LOW);
  tapaAbierta = abrir;
  Serial.println(abrir ? F("[TAPA DC] Abierta") : F("[TAPA DC] Cerrada"));
  return true;
}

// =========================================================
// SECUENCIA AUTOMÁTICA
// =========================================================
void recoger() {
  Serial.println(F("[SEQ] Iniciando secuencia RECOGER"));
  esp.println("EST:RECOGIENDO");
  if (!setPinza(PINZA_ABIERTA)) return;
  if (!mover(SUELO)) return;
  if (!setPinza(PINZA_CERRADA)) return;
  if (!mover(REPOSO) || !setTapa(true)) return;
  esp.println("EST:TAPA_ABIERTA");
  if (!mover(SOBRE_BAUL)) return;
  if (!setPinza(PINZA_ABIERTA)) return;
  if (!mover(REPOSO) || !setTapa(false)) return;
  esp.println("OK:RECOGIDO");
  Serial.println(F("[SEQ] RECOGER completado"));
}

// =========================================================
// SENSORES
// =========================================================
float leerDistancia() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 25000);  // timeout ~4 m
  return us > 0 ? (us * 0.0343f / 2.0f) : -1.0f;
}

// Envía telemetría al ESP32 cada 300 ms e imprime en el monitor
void enviarTelemetria() {
  if (millis() - tSensores < 300) return;
  tSensores = millis();

  ultimaDist = leerDistancia();
  ultimaPir  = digitalRead(PIR) == HIGH ? 1 : 0;
  // Este módulo entrega valores bajos con luz y altos a oscuras; normalizamos
  // la lectura para que el resto del sistema use bajo=oscuro y alto=claro.
  ultimaLuz  = 1023 - analogRead(LDR);

  // Formato para el ESP32: DAT:<dist>,<pir>,<luz>
  esp.print("DAT:");
  if (ultimaDist < 0) esp.print("-1.0");
  else esp.print(ultimaDist, 1);
  esp.print(",");
  esp.print(ultimaPir);
  esp.print(",");
  esp.println(ultimaLuz);
}

// =========================================================
// COMANDOS RECIBIDOS DESDE EL ESP32
// =========================================================
void procesar(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print(F("[CMD] Recibido: ")); Serial.println(cmd);

  // --- Secuencias ---
  if (cmd == "RECOGER") {
    recoger();
  }
  else if (cmd == "REPOSO" || cmd == "POS_REPOSO") {
    if (mover(REPOSO) && setPinza(PINZA_ABIERTA)) esp.println("OK:REPOSO");
  }
  else if (cmd == "POS_SUELO") {
    if (mover(SUELO)) esp.println("OK:SUELO");
  }
  else if (cmd == "POS_BAUL") {
    if (mover(SOBRE_BAUL)) esp.println("OK:BAUL");
  }
  else if (cmd == "PING") {
    esp.println("OK:PONG");
    Serial.println(F("[CMD] PONG enviado al ESP32"));
  }
  else if (cmd == "BRAZO_SUBIR" || cmd == "BRAZO_BAJAR") {
    int paso = cmd == "BRAZO_SUBIR" ? -8 : 8;
    int objetivo[3] = {
      actual[0],
      constrain(actual[1] + paso, 30, 160),
      constrain(actual[2] - paso, 20, 160)
    };
    if (mover(objetivo)) esp.println(cmd == "BRAZO_SUBIR" ? "OK:BRAZO_SUBIENDO" : "OK:BRAZO_BAJANDO");
  }
  else if (cmd == "STOP") {
    paroSolicitado = false;
    comandoEnCola = "";
    digitalWrite(TAPA_IN1, LOW);
    digitalWrite(TAPA_IN2, LOW);
    Serial.println(F("[PARO] Orden de paro recibida"));
    esp.println("OK:PARADO");
  }

  // --- Brazo libre ---
  else if (cmd == "BASE_IZQ") {
    if (!moverEje(base, P_BASE, 0, actual[0] + 10)) return;
    Serial.print(F("[SERVO] Base -> ")); Serial.println(actual[0]);
    esp.println("OK:BASE");
  }
  else if (cmd == "BASE_DER") {
    if (!moverEje(base, P_BASE, 0, actual[0] - 10)) return;
    Serial.print(F("[SERVO] Base -> ")); Serial.println(actual[0]);
    esp.println("OK:BASE");
  }
  else if (cmd == "HOMBRO_ARR") {
    if (!moverEje(hombro, P_HOMBRO, 1, actual[1] - 8)) return;
    Serial.print(F("[SERVO] Hombro -> ")); Serial.println(actual[1]);
    esp.println("OK:HOMBRO");
  }
  else if (cmd == "HOMBRO_ABA") {
    if (!moverEje(hombro, P_HOMBRO, 1, actual[1] + 8)) return;
    Serial.print(F("[SERVO] Hombro -> ")); Serial.println(actual[1]);
    esp.println("OK:HOMBRO");
  }
  else if (cmd == "CODO_ARR") {
    if (!moverEje(codo, P_CODO, 2, actual[2] + 8)) return;
    Serial.print(F("[SERVO] Codo -> ")); Serial.println(actual[2]);
    esp.println("OK:CODO");
  }
  else if (cmd == "CODO_ABA") {
    if (!moverEje(codo, P_CODO, 2, actual[2] - 8)) return;
    Serial.print(F("[SERVO] Codo -> ")); Serial.println(actual[2]);
    esp.println("OK:CODO");
  }

  // --- Pinza ---
  else if (cmd.startsWith("PINZA_POS_")) {
    int angulo = cmd.substring(10).toInt();
    angulo = constrain(angulo, PINZA_CERRADA, PINZA_ABIERTA);
    if (!setPinza(angulo)) return;
    esp.println("OK:PINZA_POS:" + String(actual[3]));
  }
  else if (cmd == "PINZA_ABRIR") {
    if (!setPinza(PINZA_ABIERTA)) return;
    esp.println("OK:PINZA_ABIERTA");
  }
  else if (cmd == "PINZA_CERRAR") {
    if (!setPinza(PINZA_CERRADA)) return;
    esp.println("OK:PINZA_CERRADA");
  }
  else if (cmd == "PINZA_TOGGLE") {
    if (!setPinza(pinzaCerrada ? PINZA_ABIERTA : PINZA_CERRADA)) return;
    esp.println(pinzaCerrada ? "OK:PINZA_CERRADA" : "OK:PINZA_ABIERTA");
  }

  // --- Tapa DC ---
  else if (cmd == "TAPA_ABRIR") {
    setTapa(true);
    esp.println("OK:TAPA_ABIERTA");
  }
  else if (cmd == "TAPA_CERRAR") {
    setTapa(false);
    esp.println("OK:TAPA_CERRADA");
  }
  else if (cmd == "TAPA_TOGGLE") {
    setTapa(!tapaAbierta);
    esp.println(tapaAbierta ? "OK:TAPA_ABIERTA" : "OK:TAPA_CERRADA");
  }

  else {
    Serial.print(F("[CMD] DESCONOCIDO: ")); Serial.println(cmd);
    esp.println("ERR:" + cmd);
  }
}

// =========================================================
// SETUP
// =========================================================
void setup() {
  Serial.begin(9600);
  esp.begin(9600);

  Serial.println(F("\n===== Raulcito bracito - Arduino UNO ====="));

  // Sensores
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(PIR,  INPUT);
  Serial.println(F("[INIT] Sensores: HC-SR04 (pines 2,4), PIR (pin 10), LDR (A0)  OK"));

  // Puente H tapa
  pinMode(TAPA_IN1, OUTPUT);
  pinMode(TAPA_IN2, OUTPUT);
  digitalWrite(TAPA_IN1, LOW);
  digitalWrite(TAPA_IN2, LOW);
  Serial.println(F("[INIT] Tapa DC puente H (pines 11, 12)  OK"));

  pinMode(BOTON_PARO, INPUT_PULLUP);
  Serial.println(F("[INIT] Boton de paro (A1 a GND) listo"));
  cargarPosiciones();
  Serial.print(F("[INIT] Posiciones guardadas: base=")); Serial.print(actual[0]);
  Serial.print(F(" hombro=")); Serial.print(actual[1]);
  Serial.print(F(" codo=")); Serial.print(actual[2]);
  Serial.print(F(" pinza=")); Serial.println(actual[3]);

  // No adjuntar ni escribir servos en el arranque: se activarán al recibir una orden.
  Serial.println(F("[INIT] Servos quietos; se activan cuando llegue una orden"));

  esp.println("OK:LISTO");
  Serial.println(F("[INIT] ESP32 notificado (OK:LISTO)"));
  Serial.println(F("=========================================="));
  Serial.println(F("Listo. Diagnostico cada 3 s. Esperando comandos del ESP32..."));
  Serial.println(F(""));
}

// =========================================================
// LOOP
// =========================================================
void loop() {
  bool presionado = digitalRead(BOTON_PARO) == LOW;
  if (presionado) {
    digitalWrite(TAPA_IN1, LOW);
    digitalWrite(TAPA_IN2, LOW);
    if (!paroFisicoActivo) Serial.println(F("[PARO] Botón presionado; secuencia/tapa detenida"));
  } else if (paroFisicoActivo) {
    Serial.println(F("[PARO] Botón liberado"));
  }
  paroFisicoActivo = presionado;

  // 1. Telemetría al ESP32
  enviarTelemetria();

  // 2. Diagnóstico en el monitor serie cada 3 s
  if (millis() - tDiagnostico >= 3000) {
    tDiagnostico = millis();
    imprimirDiagnostico();
  }

  // 3. Comandos desde el ESP32
  if (comandoEnCola.length() > 0) {
    String cmd = comandoEnCola;
    comandoEnCola = "";
    procesar(cmd);
  }
  while (esp.available()) {
    char c = esp.read();
    if (c == '\n') {
      String cmd = linea;
      linea = "";
      procesar(cmd);
    } else if (c != '\r') {
      linea += c;
    }
  }
}
