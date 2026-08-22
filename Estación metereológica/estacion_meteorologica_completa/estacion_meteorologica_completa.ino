// ============================================================
//  ESTACIÓN METEOROLÓGICA — ESP32 AP+STA + WEB LOCAL + THINGSPEAK
//  Sensores: BME280 | LTR390 | SGP30 | Pluviómetro Basculante | Anemómetro RS485
//
//  MODO WiFi: AP + STA simultáneo
//    - AP propio (siempre activo, sin necesidad de internet):
//        SSID: EstacionMeteo
//        Pass: estacion123
//        IP:   192.168.4.1  → abrir en navegador para ver la web local
//    - STA (cliente, para subir datos a ThingSpeak si hay internet):
//        SSID:
//        Pass:
//
//  Si no hay conexión a internet (STA falla), la página web local
//  sigue funcionando con normalidad vía el AP. El envío a ThingSpeak
//  simplemente se omite y se reintenta en el siguiente ciclo.
//
//  Librerías (Library Manager):
//    - Adafruit BME280 Library
//    - Adafruit LTR390 Library
//    - Adafruit SGP30 Library
//    - Adafruit Unified Sensor
//    - WebServer          (nativo del core ESP32, no requiere instalación)
//    - ArduinoJson        (by Benoit Blanchon)
//    - ModbusMaster       (by Doc Walker)
//
//  ESTRUCTURA DE ARCHIVOS (misma carpeta):
//    estacion_meteorologica_completa.ino   ← este archivo
//    pagina_web.h                          ← HTML/CSS/JS de la web
//
//  PINES:
//    Bus I2C 0 → Wire  (SDA=15, SCL=27) → BME280
//    Bus I2C 1 → Wire1 (SDA=32, SCL=33) → LTR390 + SGP30
//    Digital   → Pin 14 (INPUT_PULLUP)  → Pluviómetro basculante
//    UART2     → RX=16, TX=17, DE=25, RE=26 → Anemómetro RS485 Modbus RTU
//
//  CAMPOS THINGSPEAK ENVIADOS:
//    field1 = Temperatura       (°C)
//    field2 = Humedad           (%)
//    field3 = Presión           (hPa)
//    field4 = Velocidad viento  (m/s)
//    field5 = eCO2              (ppm)
//    field6 = TVOC              (ppb)
//    field7 = Precipitación     (~1 H equiv., mm)
//    field8 = Indice UV (OMS)
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
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

// ── TEMPORIZACIÓN ─────────────────────────────────────────
unsigned long ultimaLectura = 0;
const unsigned long INTERVALO_MS = 5000;        // lectura/serial cada 5 s
unsigned long lastPostMs = 0;
const unsigned long POST_INTERVAL_MS = 300000;  // ThingSpeak cada 5 min

// ── ACCESS POINT (siempre activo) ─────────────────────────
const char* AP_SSID = "EstacionMeteo";
const char* AP_PASS = "estacion123";

// ── STA / WiFi (para internet, si está disponible) ────────
const char* WIFI_SSID     = "Galaxy A17";
const char* WIFI_PASSWORD = "12345678";
const unsigned long WIFI_CONNECT_TIMEOUT = 15000;

// ── THINGSPEAK ─────────────────────────────────────────────
const char* THINGSPEAK_HOST = "api.thingspeak.com";
const int   THINGSPEAK_PORT = 80;
String      WRITE_API_KEY   = "G3HHRN4N0G8XNWBY";

WiFiClient client;

// ── SERVIDOR WEB (vía AP) ─────────────────────────────────
WebServer server(80);


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
  digitalWrite(RE_PIN_ANEM, HIGH);
  digitalWrite(DE_PIN_ANEM, HIGH);
}

void postTransmissionAnem() {
  digitalWrite(DE_PIN_ANEM, LOW);
  digitalWrite(RE_PIN_ANEM, LOW);
}

// ─────────────────────────────────────────────────────────
//  WiFi STA — intenta conectar sin bloquear el AP
// ─────────────────────────────────────────────────────────
void connectToWiFiSTA() {
  Serial.print("[WiFi STA] Conectando a: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi STA] Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi STA] Sin conexion a internet. Solo modo local (AP).");
  }
}

// ─────────────────────────────────────────────────────────
//  ThingSpeak — campos enviados
//    field1=temp  field2=hum   field3=pre   field4=vel_viento
//    field5=co2   field6=tvoc  field7=pluvi field8=uvi
// ─────────────────────────────────────────────────────────
bool sendToThingSpeak(float temp, float hum, float pre, float vel_vie,
                      float co2,  float tvoc,
                      float pluvi, float uvi) {

  String url = "/update?api_key=" + WRITE_API_KEY +
               "&field1=" + String(temp, 2) +
               "&field2=" + String(hum,  2) +
               "&field3=" + String(pre,  2) +
               "&field4=" + String(vel_vie, 1) +
               "&field5=" + String(co2,  1) +
               "&field6=" + String(tvoc, 1) +
               "&field7=" + String(pluvi, 4) +
               "&field8=" + String(uvi,  2);

  if (!client.connect(THINGSPEAK_HOST, THINGSPEAK_PORT)) {
    Serial.println("[HTTP] Error: No se pudo abrir conexión TCP.");
    return false;
  }

  String request = String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + THINGSPEAK_HOST + "\r\n" +
                   "Connection: close\r\n\r\n";
  client.print(request);

  unsigned long startWait = millis();
  while (!client.available() && (millis() - startWait) < 5000) delay(10);

  if (!client.available()) {
    Serial.println("[HTTP] Error: Sin respuesta del servidor.");
    client.stop();
    return false;
  }

  String response;
  while (client.available()) response += client.readStringUntil('\n') + "\n";
  client.stop();

  int lastNL = response.lastIndexOf('\n', response.length() - 2);
  String body = response.substring(lastNL + 1);
  body.trim();

  if (body == "0") {
    Serial.println("[ThingSpeak] Publicación rechazada (código 0). Verifica intervalo y API Key.");
    return false;
  }
  Serial.print("[ThingSpeak] Publicación exitosa. Entry ID: ");
  Serial.println(body);
  return true;
}


// ─────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\n==============================================");
  Serial.println("   ESTACION METEOROLOGICA - AP+STA / WEB / THINGSPEAK");
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
  digitalWrite(RE_PIN_ANEM, LOW);

  RS485Serial.begin(9600, SERIAL_8N1, RX_PIN_ANEM, TX_PIN_ANEM);
  delay(100);

  anemometro.begin(MODBUS_ADDRESS, RS485Serial);
  anemometro.preTransmission(preTransmissionAnem);
  anemometro.postTransmission(postTransmissionAnem);
  Serial.println("[OK] Anemometro RS485 listo (RX=16, TX=17, DE=25, RE=26).");

  // ── WiFi: AP + STA simultáneo ─────────────────────────
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("[WiFi AP] Activo  SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[WiFi AP] Contrasena: ");
  Serial.println(AP_PASS);
  Serial.print("[WiFi AP] Pagina local: http://");
  Serial.println(ip);

  // Intenta conectar a internet, pero el AP sigue activo igual
  connectToWiFiSTA();

  // ── Rutas del servidor web local (vía AP) ─────────────
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", HTML_PAGE);
  });

  server.on("/datos", HTTP_GET, []() {
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
    doc["internet"]       = (WiFi.status() == WL_CONNECTED);

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "no encontrado");
  });

  server.begin();
  Serial.println("[OK] Servidor web local iniciado.\n");

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
  lastPostMs    = millis() - POST_INTERVAL_MS; // fuerza primer envío si hay internet

  Serial.println("==============================================");
  Serial.println("   Todo listo. Iniciando ciclo de lectura.");
  Serial.println("==============================================\n");
}


// ─────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────
unsigned long ultimaLecturaSGP = 0;

void loop() {
  server.handleClient();

  unsigned long ahora = millis();

  // SGP30: leer exactamente cada 1000 ms (requisito del sensor)
  if (ahora - ultimaLecturaSGP >= 1000) {
    ultimaLecturaSGP = ahora;
    if (!sgp.IAQmeasure()) {
      Serial.println("[SGP30] Error de lectura.");
    }
  }

  // Lecturas completas cada INTERVALO_MS (5 s)
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
    Serial.printf("[WiFi]    AP activo | STA: %s\n",
                  (WiFi.status() == WL_CONNECTED) ? "conectado (internet OK)" : "sin conexion");
    Serial.println("----------------------------------------------\n");

    // ════════════════════════════════════════════════════
    //  THINGSPEAK — solo si hay internet, cada POST_INTERVAL_MS
    // ════════════════════════════════════════════════════
    if (millis() - lastPostMs >= POST_INTERVAL_MS) {

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi STA] Desconectado. Reintentando...");
        connectToWiFiSTA();
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[ThingSpeak] Enviando datos...");
        bool ok = sendToThingSpeak(datos.temperatura, datos.humedad, datos.presion,
                                    datos.vientoMs, (float)datos.eco2, (float)datos.tvoc,
                                    datos.lluviaPorHora, datos.uvi);
        if (!ok) Serial.println("[Main] Fallo el envio a ThingSpeak. Se reintentara despues.");
      } else {
        Serial.println("[Main] Sin internet. Solo modo local (AP). No se envia a ThingSpeak.");
      }

      lastPostMs = millis();
    }
  }

  // Sin delay() largo en el loop — server.handleClient() necesita
  // ejecutarse frecuentemente para atender las peticiones web
}
