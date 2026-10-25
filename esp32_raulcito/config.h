// ===== Configuración guardada en NVS (Preferences) =====
#pragma once
#include <Arduino.h>

#define PIN_DEFECTO     "1234"          // PIN del panel si no se define otro
#define NOMBRE_DEFECTO  "raulcito-01"   // identifica al robot en la BD

struct Config {
  String ssid, pass;                // red Wi-Fi
  String appKey, appSecret;         // credenciales SinricPro
  String idRobot, idPir, idLdr;     // Device IDs: Switch, Motion, Contact
  String urlApi;                    // URL de guardar.php
  String nombre;                    // nombre del robot
  String pin;                       // PIN de 4 dígitos del panel
};

extern Config cfg;                  // configuración activa (no cambia hasta reiniciar)

void   configCargar();
void   configGuardar(const Config &c, bool desdeNube = false);   // local = se sube a la página
void   configBorrar();
bool   configSubirPendiente();             // hay cambios locales que subir a la página
void   configSubirHecho();
bool   configHayWifi();                    // hay una red guardada
bool   configSinricOk(const Config &c);    // APP_KEY y APP_SECRET con formato válido
String configValidar(const Config &c);     // "" si todo bien; si no, el motivo

bool formatoAppKey(const String &s);
bool formatoAppSecret(const String &s);
bool formatoDeviceId(const String &s);
