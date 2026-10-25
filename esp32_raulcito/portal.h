// ===== Portal web: página única + API JSON =====
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void portalIniciar(bool modoAP);   // modoAP = portal cautivo y sin PIN
void portalLoop();
void portalEvento(const String &tipo, const String &valor, const String &fecha);

// Implementadas en esp32_raulcito.ino (lógica del robot)
void   robotEstado(JsonObject o);          // sensores y estados para /api/status
String robotComando(const String &cmd);    // "" = OK; si no, el mensaje de error
