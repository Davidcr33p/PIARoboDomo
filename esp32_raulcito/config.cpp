// ===== Configuración guardada en NVS (Preferences) =====
#include "config.h"
#include <Preferences.h>

Config cfg;
static const char *NS = "raulcito";   // espacio de nombres en la NVS

void configCargar() {
  Preferences p;
  p.begin(NS, false);
  cfg.ssid      = p.getString("ssid", "");
  cfg.pass      = p.getString("pass", "");
  cfg.appKey    = p.getString("appKey", "");
  cfg.appSecret = p.getString("appSecret", "");
  cfg.idRobot   = p.getString("idRobot", "");
  cfg.idPir     = p.getString("idPir", "");
  cfg.idLdr     = p.getString("idLdr", "");
  cfg.urlApi    = p.getString("url", "");
  cfg.nombre    = p.getString("nombre", NOMBRE_DEFECTO);
  cfg.pin       = p.getString("pin", PIN_DEFECTO);
  p.end();
}

void configGuardar(const Config &c) {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", c.ssid);
  p.putString("pass", c.pass);
  p.putString("appKey", c.appKey);
  p.putString("appSecret", c.appSecret);
  p.putString("idRobot", c.idRobot);
  p.putString("idPir", c.idPir);
  p.putString("idLdr", c.idLdr);
  p.putString("url", c.urlApi);
  p.putString("nombre", c.nombre);
  p.putString("pin", c.pin);
  p.end();
}

void configBorrar() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

bool configHayWifi() { return cfg.ssid.length() > 0; }

// ---------- VALIDACIONES (mismas reglas que usa la librería SinricPro) ----------
static bool guionesEn(const String &s, std::initializer_list<int> posiciones) {
  for (int p : posiciones)
    if (s[p] != '-') return false;
  return true;
}

bool formatoAppKey(const String &s) {
  return s.length() == 36 && guionesEn(s, {8, 13, 18, 23});
}

bool formatoAppSecret(const String &s) {
  return s.length() == 73 && guionesEn(s, {8, 13, 18, 23, 36, 45, 50, 55, 60});
}

bool formatoDeviceId(const String &s) {
  if (s.length() != 24) return false;
  for (unsigned i = 0; i < s.length(); i++)
    if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

bool configSinricOk(const Config &c) {
  return formatoAppKey(c.appKey) && formatoAppSecret(c.appSecret);
}

String configValidar(const Config &c) {
  if (c.ssid.isEmpty() || c.ssid.length() > 32)
    return "El nombre de la red (SSID) debe tener de 1 a 32 caracteres";
  if (c.pass.length() && (c.pass.length() < 8 || c.pass.length() > 63))
    return "La contraseña Wi-Fi debe tener de 8 a 63 caracteres (vacía si la red es abierta)";
  if (c.pin.length() != 4 || !isDigit(c.pin[0]) || !isDigit(c.pin[1]) || !isDigit(c.pin[2]) || !isDigit(c.pin[3]))
    return "El PIN debe ser de 4 dígitos";
  if (c.nombre.isEmpty() || c.nombre.length() > 32)
    return "El nombre del robot debe tener de 1 a 32 caracteres";
  if (c.urlApi.length() && !c.urlApi.startsWith("http://") && !c.urlApi.startsWith("https://"))
    return "La URL de la API debe empezar con http:// o https://";
  if (c.appKey.length() && !formatoAppKey(c.appKey))
    return "APP_KEY inválida: son 36 caracteres (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)";
  if (c.appSecret.length() && !formatoAppSecret(c.appSecret))
    return "APP_SECRET inválido: son 73 caracteres con guiones";
  if (c.appKey.isEmpty() != c.appSecret.isEmpty())
    return "Captura APP_KEY y APP_SECRET juntos";
  if (c.idRobot.length() && !formatoDeviceId(c.idRobot))
    return "Device ID de Raulcito inválido (24 caracteres hexadecimales)";
  if (c.idPir.length() && !formatoDeviceId(c.idPir))
    return "Device ID de Movimiento inválido (24 caracteres hexadecimales)";
  if (c.idLdr.length() && !formatoDeviceId(c.idLdr))
    return "Device ID de Oscuridad inválido (24 caracteres hexadecimales)";
  return "";
}
