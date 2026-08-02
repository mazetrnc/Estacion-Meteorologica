// ============================================================
//  ESTACIÓN METEOROLÓGICA — ESP32 ACCESS POINT + WEB SERVER
//  Sensores: BME280 | LTR390 | SGP30 | Pluviómetro Basculante | Anemómetro RS485
//
//  Modo:  Access Point propio (sin router externo)
//  SSID:  EstacionMeteo
//  Pass:  estacion123
//  IP:    192.168.4.1  →  abrir en cualquier navegador
//
//  Librerías (Library Manager):
//    - Adafruit BME280 Library
//    - Adafruit LTR390 Library
//    - Adafruit SGP30 Library
//    - Adafruit Unified Sensor
//    - ESPAsyncWebServer  (by ESP Async Team)
//    - AsyncTCP           (by ESP Async Team)
//    - ArduinoJson        (by Benoit Blanchon)
//    - ModbusMaster       (by Doc Walker)
//
//  ESTRUCTURA DE ARCHIVOS (misma carpeta):
//    estacion_meteorologica_web_local.ino   ← este archivo
//    pagina_web.h                           ← HTML/CSS/JS de la web
//
//  PINES:
//    Bus I2C 0 → Wire  (SDA=15, SCL=27) → BME280
//    Bus I2C 1 → Wire1 (SDA=32, SCL=33) → LTR390 + SGP30
//    Digital   → Pin 14 (INPUT_PULLUP)  → Pluviómetro basculante
//    UART2     → RX=16, TX=17, DE=25, RE=26 → Anemómetro RS485 Modbus RTU
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>

// El HTML está en un .h separado para evitar que el
// preprocesador de Arduino confunda JS con código C++
#include "pagina_web.h"

// ── BME280 ────────────────────────────────────────────────
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#define SDA_BME  15
#define SCL_BME  27
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

// ── LTR390 ───────────────────────────────────────────────
#include <Adafruit_LTR390.h>
#define SDA_I2C1 32
#define SCL_I2C1 33
Adafruit_LTR390 ltr = Adafruit_LTR390();

// ── SGP30 ────────────────────────────────────────────────
#include "Adafruit_SGP30.h"
Adafruit_SGP30 sgp;
int contadorCalentamiento = 0;

// ── PLUVIÓMETRO BASCULANTE ────────────────────────────────
const int        pinPluviometro    = 14;
volatile int     conteoPulsos      = 0;
unsigned long    ultimoTiempoRebote = 0;
const int        tiempoAntiRebote  = 200;
const float      MM_POR_PULSO      = 0.2794;
unsigned long    tiempoInicioMs    = 0;

// ── ANEMÓMETRO RS485 MODBUS RTU ───────────────────────────
#define RX_PIN_ANEM   16
#define TX_PIN_ANEM   17
#define DE_PIN_ANEM   25
#define RE_PIN_ANEM   26
#define MODBUS_ADDRESS  3

HardwareSerial RS485Serial(2);
ModbusMaster anemometro;

// ── DATOS GLOBALES (compartidos con el servidor web) ──────
struct DatosEstacion {
  float         temperatura;
  float         presion;
  float         altitud;
  float         humedad;
  float         lux;
  float         uvi;
  uint32_t      uvRaw;
  uint16_t      eco2;
  uint16_t      tvoc;
  float         mmAcumulados;
  float         lluviaPorHora;
  int           pulsos;
  float         horasOperacion;
  bool          sgpCalentando;
  float         vientoMs;
  float         vientoKmh;
  float         vientoNudos;
  bool          vientoOK;
} datos;

// ── TEMPORIZADOR LECTURA ──────────────────────────────────
unsigned long ultimaLectura = 0;
const unsigned long INTERVALO_MS = 5000;

// ── ACCESS POINT ──────────────────────────────────────────
const char* AP_SSID = "EstacionMeteo";
const char* AP_PASS = "estacion123";

// ── SERVIDOR WEB ASÍNCRONO ────────────────────────────────
AsyncWebServer server(80);


// ─────────────────────────────────────────────────────────
//  ISR: Pluviómetro
// ─────────────────────────────────────────────────────────
void IRAM_ATTR contarVueltaCubeta() {
  unsigned long t = millis();
  if (t - ultimoTiempoRebote > (unsigned long)tiempoAntiRebote) {
    conteoPulsos++;
    ultimoTiempoRebote = t;
  }
}

// ─────────────────────────────────────────────────────────
//  Callbacks RS485 para controlar DE y RE del anemómetro
// ─────────────────────────────────────────────────────────
void preTransmissionAnem() {
  digitalWrite(RE_PIN_ANEM, HIGH); // RE HIGH = deshabilitar receptor
  digitalWrite(DE_PIN_ANEM, HIGH); // DE HIGH = habilitar driver (TX)
}

void postTransmissionAnem() {
  digitalWrite(DE_PIN_ANEM, LOW);  // DE LOW = deshabilitar driver
  digitalWrite(RE_PIN_ANEM, LOW);  // RE LOW = habilitar receptor (RX)
}


// ─────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\n==============================================");
  Serial.println("   ESTACION METEOROLOGICA + WEB SERVER");
  Serial.println("==============================================\n");

  // ── Bus I2C 0: BME280 ─────────────────────────────────
  Wire.begin(SDA_BME, SCL_BME);
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] BME280 no encontrado en SDA=15 SCL=27.");
    while (1);
  }
  Serial.println("[OK] BME280 listo.");

  // ── Bus I2C 1: LTR390 + SGP30 ────────────────────────
  Wire1.begin(SDA_I2C1, SCL_I2C1);

  if (!ltr.begin(&Wire1)) {
    Serial.println("[ERROR] LTR390 no encontrado en SDA=32 SCL=33.");
    while (1);
  }
  ltr.setGain(LTR390_GAIN_18);
  ltr.setResolution(LTR390_RESOLUTION_16BIT);
  Serial.println("[OK] LTR390 listo.");

  if (!sgp.begin(&Wire1)) {
    Serial.println("[ERROR] SGP30 no encontrado en SDA=32 SCL=33.");
    while (1);
  }
  Serial.println("[OK] SGP30 listo.");

  // ── Pluviómetro ───────────────────────────────────────
  pinMode(pinPluviometro, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinPluviometro),
                  contarVueltaCubeta, FALLING);
  tiempoInicioMs = millis();
  Serial.println("[OK] Pluviometro listo en Pin 14.");

  // ── Anemómetro RS485 Modbus RTU ───────────────────────
  pinMode(DE_PIN_ANEM, OUTPUT);
  pinMode(RE_PIN_ANEM, OUTPUT);
  digitalWrite(DE_PIN_ANEM, LOW);
  digitalWrite(RE_PIN_ANEM, LOW); // Modo recepción por defecto

  RS485Serial.begin(9600, SERIAL_8N1, RX_PIN_ANEM, TX_PIN_ANEM);
  delay(100);

  anemometro.begin(MODBUS_ADDRESS, RS485Serial);
  anemometro.preTransmission(preTransmissionAnem);
  anemometro.postTransmission(postTransmissionAnem);
  Serial.println("[OK] Anemometro RS485 listo (RX=16, TX=17, DE=25, RE=26).");

  // ── Access Point WiFi ─────────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("[WiFi] Access Point activo  SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[WiFi] Contrasena: ");
  Serial.println(AP_PASS);
  Serial.print("[WiFi] Abrir en navegador: http://");
  Serial.println(ip);

  // ── Rutas del servidor ────────────────────────────────

  // GET /  →  página web completa
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", HTML_PAGE);
  });

  // GET /datos  →  JSON con lecturas actuales
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<512> doc;
    doc["temperatura"]    = datos.temperatura;
    doc["presion"]        = datos.presion;
    doc["altitud"]        = datos.altitud;
    doc["humedad"]        = datos.humedad;
    doc["lux"]            = datos.lux;
    doc["uvi"]            = datos.uvi;
    doc["uvRaw"]          = datos.uvRaw;
    doc["eco2"]           = datos.eco2;
    doc["tvoc"]           = datos.tvoc;
    doc["mmAcumulados"]   = datos.mmAcumulados;
    doc["lluviaPorHora"]  = datos.lluviaPorHora;
    doc["pulsos"]         = datos.pulsos;
    doc["horasOperacion"] = datos.horasOperacion;
    doc["sgpCalentando"]  = datos.sgpCalentando;
    doc["vientoMs"]       = datos.vientoMs;
    doc["vientoKmh"]      = datos.vientoKmh;
    doc["vientoNudos"]    = datos.vientoNudos;
    doc["vientoOK"]       = datos.vientoOK;
    doc["uptime"]         = millis() / 1000UL;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "no encontrado");
  });

  server.begin();
  Serial.println("[OK] Servidor web iniciado.\n");

  // ── Lectura inicial para no servir datos vacíos ───────
  datos.temperatura   = bme.readTemperature();
  datos.presion       = bme.readPressure() / 100.0F;
  datos.altitud       = bme.readAltitude(SEALEVELPRESSURE_HPA);
  datos.humedad       = bme.readHumidity();
  datos.lux           = 0;
  datos.uvi           = 0;
  datos.uvRaw         = 0;
  datos.eco2          = 400;
  datos.tvoc          = 0;
  datos.mmAcumulados  = 0;
  datos.lluviaPorHora = 0;
  datos.pulsos        = 0;
  datos.horasOperacion = 0;
  datos.sgpCalentando = true;
  datos.vientoMs      = 0;
  datos.vientoKmh     = 0;
  datos.vientoNudos   = 0;
  datos.vientoOK      = false;

  ultimaLectura = millis();
}


// ─────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────
unsigned long ultimaLecturaSGP = 0;

void loop() {
  unsigned long ahora = millis();

  // SGP30: leer exactamente cada 1000 ms (requisito del sensor)
  if (ahora - ultimaLecturaSGP >= 1000) {
    ultimaLecturaSGP = ahora;
    if (!sgp.IAQmeasure()) {
      Serial.println("[SGP30] Error de lectura.");
    }
  }

  // Lecturas completas cada INTERVALO_MS
  if (ahora - ultimaLectura >= INTERVALO_MS) {
    ultimaLectura = ahora;

    // ── BME280 ──────────────────────────────────────────
    datos.temperatura = bme.readTemperature();
    datos.presion     = bme.readPressure() / 100.0F;
    datos.altitud     = bme.readAltitude(SEALEVELPRESSURE_HPA);
    datos.humedad     = bme.readHumidity();

    // ── LTR390 ──────────────────────────────────────────
    ltr.setMode(LTR390_MODE_UVS);
    delay(100);
    if (ltr.newDataAvailable()) {
      datos.uvRaw = ltr.readUVS();
      datos.uvi   = (float)datos.uvRaw / 2300.0;
    }
    ltr.setMode(LTR390_MODE_ALS);
    delay(100);
    if (ltr.newDataAvailable()) {
      uint32_t als = ltr.readALS();
      datos.lux    = als * 0.6;
    }

    // ── SGP30 ────────────────────────────────────────────
    datos.eco2 = sgp.eCO2;
    datos.tvoc = sgp.TVOC;
    if (contadorCalentamiento < 15) {
      contadorCalentamiento++;
      datos.sgpCalentando = true;
    } else {
      datos.sgpCalentando = false;
    }

    // ── Pluviómetro ──────────────────────────────────────
    datos.horasOperacion = (float)(millis() - tiempoInicioMs) / 3600000.0;
    datos.mmAcumulados   = conteoPulsos * MM_POR_PULSO;
    datos.pulsos         = conteoPulsos;
    datos.lluviaPorHora  = (datos.horasOperacion > 0.0)
                           ? (datos.mmAcumulados / datos.horasOperacion)
                           : 0.0;

    // ── Anemómetro RS485 ──────────────────────────────────
    uint8_t resultAnem = anemometro.readHoldingRegisters(0x0000, 1);
    if (resultAnem == anemometro.ku8MBSuccess) {
      uint16_t raw       = anemometro.getResponseBuffer(0);
      datos.vientoMs    = raw / 10.0;
      datos.vientoKmh   = datos.vientoMs * 3.6;
      datos.vientoNudos = datos.vientoMs * 1.94384;
      datos.vientoOK    = true;
    } else {
      datos.vientoOK = false;
    }

    // ── Serial ───────────────────────────────────────────
    Serial.println("----------------------------------------------");
    Serial.printf("[PLUVIOMETRO] Pulsos: %d | %.4f mm | %.4f mm/h\n",
                  conteoPulsos, datos.mmAcumulados, datos.lluviaPorHora);
    Serial.printf("[BME280]  T: %.2f C | P: %.2f hPa | H: %.2f %%\n",
                  datos.temperatura, datos.presion, datos.humedad);
    Serial.printf("[LTR390]  Lux: %.1f | UVI: %.2f\n",
                  datos.lux, datos.uvi);
    Serial.printf("[SGP30]   eCO2: %d ppm | TVOC: %d ppb%s\n",
                  datos.eco2, datos.tvoc,
                  datos.sgpCalentando ? " [calentando]" : "");
    if (datos.vientoOK) {
      Serial.printf("[VIENTO]  %.1f m/s | %.1f km/h | %.1f nudos\n",
                    datos.vientoMs, datos.vientoKmh, datos.vientoNudos);
    } else {
      Serial.printf("[VIENTO]  [ERROR] Codigo Modbus: 0x%02X\n", resultAnem);
    }
    Serial.println("----------------------------------------------\n");
  }

  // Sin delay() aquí — el loop corre libre y el WDT no se dispara
}
