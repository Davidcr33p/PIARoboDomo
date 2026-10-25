// ===== Configuración por Bluetooth (BLE) con NimBLE =====
#include "ble_setup.h"
#include "config.h"
#include <WiFi.h>
#include <NimBLEDevice.h>

#define BLE_NOMBRE "Raulcito-BLE"

// UUIDs propios ("7261756c6369746f" = "raulcito" en hexadecimal)
#define UUID_SERVICIO "7a1e0000-7261-756c-6369-746f00000000"
#define UUID_GUARDAR  "7a1e00f0-7261-756c-6369-746f00000000"
#define UUID_ESTADO   "7a1e00f1-7261-756c-6369-746f00000000"

// Campos de texto que se escriben desde el celular
struct CampoBle {
  const char *uuid;
  const char *nombre;       // aparece en nRF Connect (descriptor 0x2901)
  String Config::*dato;     // a qué campo de Config va
  bool legible;             // los secretos no se pueden leer de vuelta
  bool recortar;            // quitar espacios (no en la contraseña Wi-Fi)
};

static const CampoBle CAMPOS[] = {
  {"7a1e0001-7261-756c-6369-746f00000000", "SSID",       &Config::ssid,      true,  true},
  {"7a1e0002-7261-756c-6369-746f00000000", "PASSWORD",   &Config::pass,      false, false},
  {"7a1e0003-7261-756c-6369-746f00000000", "APP_KEY",    &Config::appKey,    false, true},
  {"7a1e0004-7261-756c-6369-746f00000000", "APP_SECRET", &Config::appSecret, false, true},
  {"7a1e0005-7261-756c-6369-746f00000000", "ID_ROBOT",   &Config::idRobot,   true,  true},
  {"7a1e0006-7261-756c-6369-746f00000000", "ID_PIR",     &Config::idPir,     true,  true},
  {"7a1e0007-7261-756c-6369-746f00000000", "ID_LDR",     &Config::idLdr,     true,  true},
  {"7a1e0008-7261-756c-6369-746f00000000", "URL_API",    &Config::urlApi,    true,  true},
  {"7a1e0009-7261-756c-6369-746f00000000", "PIN",        &Config::pin,       false, true},
  {"7a1e000a-7261-756c-6369-746f00000000", "NOMBRE",     &Config::nombre,    true,  true},
};

enum FaseBle { ESPERA, PROBANDO, REINICIANDO };

static Config pendiente;                   // lo que va llegando por BLE
static Config prueba;                      // copia que se está probando
static NimBLEServer *servidor = nullptr;
static NimBLECharacteristic *chrEstado = nullptr;
static volatile bool pedirGuardar = false;
static volatile FaseBle fase = ESPERA;
static unsigned long tFase = 0;

// Actualiza la característica ESTADO y avisa al celular
static void ponerEstado(const String &s) {
  chrEstado->setValue(s.c_str());
  chrEstado->notify();
  Serial.println("[BLE] Estado: " + s);
}

// Guarda el texto escrito en el campo correspondiente
class AlEscribirCampo : public NimBLECharacteristicCallbacks {
 public:
  explicit AlEscribirCampo(const CampoBle &c) : campo(c) {}
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    if (fase != ESPERA) return;
    std::string v = chr->getValue();
    String s(v.c_str());
    s.replace("\r", "");
    s.replace("\n", "");
    if (campo.recortar) s.trim();
    pendiente.*(campo.dato) = s;
    Serial.printf("[BLE] %s recibido\n", campo.nombre);   // sin imprimir el valor
  }
 private:
  const CampoBle &campo;
};

// GUARDAR: acepta "1" como texto o el byte 0x01
class AlGuardar : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    std::string v = chr->getValue();
    if (!v.empty() && (v[0] == '1' || v[0] == 1) && fase == ESPERA) pedirGuardar = true;
  }
};

void bleIniciar() {
  pendiente = cfg;                         // lo no escrito conserva lo guardado
  NimBLEDevice::init(BLE_NOMBRE);
  NimBLEDevice::setMTU(517);               // para escribir APP_SECRET (73 caracteres) de una vez
  servidor = NimBLEDevice::createServer();
  servidor->advertiseOnDisconnect(true);
  NimBLEService *svc = servidor->createService(UUID_SERVICIO);

  for (const CampoBle &c : CAMPOS) {
    uint32_t props = NIMBLE_PROPERTY::WRITE;
    if (c.legible) props |= NIMBLE_PROPERTY::READ;
    NimBLECharacteristic *chr = svc->createCharacteristic(c.uuid, props, 128);
    if (c.legible) chr->setValue((pendiente.*(c.dato)).c_str());
    chr->setCallbacks(new AlEscribirCampo(c));
    chr->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue(c.nombre);
  }

  NimBLECharacteristic *guardar = svc->createCharacteristic(UUID_GUARDAR, NIMBLE_PROPERTY::WRITE, 4);
  guardar->setCallbacks(new AlGuardar());
  guardar->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue("GUARDAR");

  chrEstado = svc->createCharacteristic(UUID_ESTADO, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  chrEstado->setValue("ESPERANDO");
  chrEstado->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue("ESTADO");

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();   // al anunciar arranca el servidor GATT
  adv->enableScanResponse(true);           // el nombre va en la respuesta de escaneo
  adv->setName(BLE_NOMBRE);
  adv->addServiceUUID(UUID_SERVICIO);
  adv->start();
  Serial.println("BLE activo: \"" BLE_NOMBRE "\" (configura con nRF Connect)");
}

// Tras GUARDAR=1 prueba la red; solo guarda en NVS si se conecta
void bleLoop() {
  if (!chrEstado) return;
  switch (fase) {
    case ESPERA: {
      if (!pedirGuardar) return;
      pedirGuardar = false;
      prueba = pendiente;
      if (prueba.nombre.isEmpty()) prueba.nombre = NOMBRE_DEFECTO;
      if (prueba.pin.isEmpty()) prueba.pin = PIN_DEFECTO;
      String err = configValidar(prueba);
      if (err.length()) {
        ponerEstado("ERROR:" + err);
        return;
      }
      ponerEstado("CONECTANDO");
      WiFi.setAutoReconnect(true);         // reintenta durante la prueba
      WiFi.begin(prueba.ssid.c_str(), prueba.pass.length() ? prueba.pass.c_str() : nullptr);
      tFase = millis();
      fase = PROBANDO;
      break;
    }
    case PROBANDO:
      if (WiFi.status() == WL_CONNECTED) {
        configGuardar(prueba);
        ponerEstado("OK:" + WiFi.localIP().toString());
        tFase = millis();
        fase = REINICIANDO;
      } else if (millis() - tFase > 20000) {
        WiFi.setAutoReconnect(false);      // que no siga buscando y mueva el canal del AP
        WiFi.disconnect();
        ponerEstado("ERROR:no se pudo conectar a " + prueba.ssid);
        fase = ESPERA;
      }
      break;
    case REINICIANDO:
      if (millis() - tFase > 5000) ESP.restart();   // tiempo para leer la IP en el celular
      break;
  }
}

bool bleOcupado() {
  return (servidor && servidor->getConnectedCount() > 0) || fase != ESPERA;
}
