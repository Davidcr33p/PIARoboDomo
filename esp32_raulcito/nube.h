// ===== Página de internet (Firebase Realtime Database) =====
// El ESP32 sube su estado cada segundo y ejecuta las órdenes que llegan de
// https://raulcito-bracito.web.app. Todo corre en una tarea aparte.
#pragma once
#include <Arduino.h>

#define PAGINA_WEB "https://raulcito-bracito.web.app"

void   nubeIniciar();       // solo en modo conectado
void   nubeLoop();          // en el loop: ejecuta órdenes y arma el estado
void   nubeEvento(const String &tipo, const String &valor, const String &fecha);
String nubeEstadoTexto();   // para el panel local
