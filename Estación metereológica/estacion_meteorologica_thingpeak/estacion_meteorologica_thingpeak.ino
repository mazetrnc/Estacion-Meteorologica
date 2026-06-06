// ============================================================
//  ESTACIÓN METEOROLÓGICA UNIFICADA — ThingSpeak
//  Sensores : BME280 | LTR390 | SGP30 | Pluviómetro basculante
// ============================================================
//
//  Bus I2C 0 → Wire  (SDA=26, SCL=27)  → BME280
//  Bus I2C 1 → Wire1 (SDA=32, SCL=33)  → LTR390 + SGP30
//  Digital   → Pin 14 (INPUT_PULLUP)   → Pluviómetro basculante
//
//  Campos ThingSpeak enviados:
//    field1 = Temperatura   (°C)
//    field2 = Humedad       (%)
//    field3 = Presión       (hPa)
//    field4 = — (vel_viento, NO SE ENVÍA)
//    field5 = eCO2          (ppm)
//    field6 = TVOC          (ppb)
//    field7 = Precipitación (~1 H equiv., mm)
//    field8 = Luz visible   (Lux)
// ============================================================

#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>

// ── BME280 ────────────────────────────────────────────────
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#define SDA_BME  26
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
const int pinPluviometro        = 14;
volatile int conteoPulsos       = 0;
unsigned long ultimoTiempoRebote = 0;
const int tiempoAntiRebote      = 200;   // ms anti-rebote mecánico
const float MM_POR_PULSO        = 0.2794;
unsigned long tiempoInicioMs    = 0;

const char* WIFI_SSID     = "Infinix HOT 50 Pro+";      // <-- Cambia por el nombre de tu red WiFi
const char* WIFI_PASSWORD = "12345678";  // <-- Cambia por la contraseña de tu WiFi

// ── CONFIGURACIÓN THINGSPEAK ─────────────────────────────
const char* THINGSPEAK_HOST       = "api.thingspeak.com";
const int   THINGSPEAK_PORT       = 80;
String      WRITE_API_KEY         = "G3HHRN4N0G8XNWBY"; // <-- Tu Write API Key
const unsigned long POST_INTERVAL_MS     = 600000; // 15 min
const unsigned long WIFI_CONNECT_TIMEOUT = 20000;

// ── TEMPORIZACIÓN ─────────────────────────────────────────
const unsigned long INTERVALO_SERIAL_MS = 5000;   // Serial cada 5 s
unsigned long lastPostMs      = 0;
unsigned long ultimaLectura   = 0;

WiFiClient client;

// ─────────────────────────────────────────────────────────
//  ISR: Pluviómetro
// ─────────────────────────────────────────────────────────
void IRAM_ATTR contarVueltaCubeta() {
  unsigned long t = millis();
  if (t - ultimoTiempoRebote > tiempoAntiRebote) {
    conteoPulsos++;
    ultimoTiempoRebote = t;
  }
}

// ─────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────
void connectToWiFi() {
  Serial.println();
  Serial.print("[WiFi] Conectando a: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] No fue posible conectar dentro del tiempo límite.");
  }
}

// ─────────────────────────────────────────────────────────
//  ThingSpeak — campos enviados (field4/vel_viento omitido)
//    field1=temp  field2=hum   field3=pre
//    field5=co2   field6=tvoc  field7=pluvi  field8=lux
// ─────────────────────────────────────────────────────────
bool sendToThingSpeak(float temp, float hum, float pre,
                      float co2,  float tvoc,
                      float pluvi, float lux) {

  String url = "/update?api_key=" + WRITE_API_KEY +
               "&field1=" + String(temp, 2) +
               "&field2=" + String(hum,  2) +
               "&field3=" + String(pre,  2) +
               // "&field4=" + String(vel_vie, 1) +
               "&field5=" + String(co2,  1) +
               "&field6=" + String(tvoc, 1) +
               "&field7=" + String(pluvi, 4) +
               "&field8=" + String(lux,  2);

  Serial.print("[HTTP] Conectando a ");
  Serial.print(THINGSPEAK_HOST);
  Serial.print(":");
  Serial.println(THINGSPEAK_PORT);

  if (!client.connect(THINGSPEAK_HOST, THINGSPEAK_PORT)) {
    Serial.println("[HTTP] Error: No se pudo abrir conexión TCP.");
    return false;
  }

  String request = String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + THINGSPEAK_HOST + "\r\n" +
                   "Connection: close\r\n\r\n";
  client.print(request);
  Serial.println("[HTTP] Petición enviada.");

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

  Serial.println("[HTTP] Respuesta ThingSpeak:");
  Serial.println("--------------------------------");
  Serial.println(response);
  Serial.println("--------------------------------");

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
  Serial.println("   ESTACIÓN METEOROLÓGICA — Iniciando...");
  Serial.println("==============================================\n");

  // Bus I2C 0: BME280
  Wire.begin(SDA_BME, SCL_BME);
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] BME280 no encontrado (SDA=26 SCL=27). Verifica cables.");
    while (1);
  }
  Serial.println("[OK] BME280 detectado.");

  // Bus I2C 1: LTR390 + SGP30
  Wire1.begin(SDA_I2C1, SCL_I2C1);

  if (!ltr.begin(&Wire1)) {
    Serial.println("[ERROR] LTR390 no encontrado (SDA=32 SCL=33). Verifica cables.");
    while (1);
  }
  ltr.setGain(LTR390_GAIN_18);
  ltr.setResolution(LTR390_RESOLUTION_16BIT);
  Serial.println("[OK] LTR390 detectado.");

  if (!sgp.begin(&Wire1)) {
    Serial.println("[ERROR] SGP30 no encontrado (SDA=32 SCL=33). Verifica cables.");
    while (1);
  }
  Serial.println("[OK] SGP30 detectado.");

  // Pluviómetro
  pinMode(pinPluviometro, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinPluviometro), contarVueltaCubeta, FALLING);
  tiempoInicioMs = millis();
  Serial.println("[OK] Pluviómetro basculante configurado (Pin 14).");

  // WiFi
  connectToWiFi();

  lastPostMs    = millis() - POST_INTERVAL_MS; // fuerza envío inmediato
  ultimaLectura = millis();

  Serial.println("\n==============================================");
  Serial.println("   Todo listo. Iniciando ciclo de lectura.");
  Serial.println("==============================================\n");
}

// ─────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────
void loop() {

  // SGP30: lectura recomendada cada 1 segundo
  if (!sgp.IAQmeasure()) {
    Serial.println("[SGP30] Error al leer datos.");
  }

  unsigned long ahora = millis();

  // ── Bloque de lectura y serial cada INTERVALO_SERIAL_MS ──
  if (ahora - ultimaLectura >= INTERVALO_SERIAL_MS) {
    ultimaLectura = ahora;

    // ── Pluviómetro ──────────────────────────────────────
    float horasOperacion = (float)(millis() - tiempoInicioMs) / 3600000.0;
    float mmAcumulados   = conteoPulsos * MM_POR_PULSO;
    float lluviaPorHora  = (horasOperacion > 0) ? (mmAcumulados / horasOperacion) : 0.0;

    // ── BME280 ───────────────────────────────────────────
    float temp = bme.readTemperature();
    float hum  = bme.readHumidity();
    float pre  = bme.readPressure() / 100.0F;
    float alt  = bme.readAltitude(SEALEVELPRESSURE_HPA);

    // ── LTR390 ───────────────────────────────────────────
    float luxData = 0.0;
    float uvi     = 0.0;
    uint32_t uvData = 0;

    ltr.setMode(LTR390_MODE_UVS);
    delay(100);
    if (ltr.newDataAvailable()) {
      uvData = ltr.readUVS();
      uvi    = (float)uvData / 2300.0;

      ltr.setMode(LTR390_MODE_ALS);
      delay(100);
      uint32_t alsData = ltr.readALS();
      luxData = alsData * 0.6;
    }

    // ── SGP30 (valores ya actualizados por IAQmeasure) ───
    float co2  = (float)sgp.eCO2;
    float tvoc = (float)sgp.TVOC;

    // ════════════════════════════════════════════════════
    //  SERIAL COMPLETO (todas las variables)
    // ════════════════════════════════════════════════════
    Serial.println("----------------------------------------------");

    Serial.println("[PLUVIÓMETRO BASCULANTE]");
    Serial.print("  Tiempo de operacion:\t");
    Serial.print(horasOperacion, 4);
    Serial.println(" H");
    Serial.print("  Vuelcos de cubeta:\t");
    Serial.println(conteoPulsos);
    Serial.print("  Precipitacion total:\t");
    Serial.print(mmAcumulados, 4);
    Serial.println(" mm");
    Serial.print("  Tasa (~1 H equiv.):\t");
    Serial.print(lluviaPorHora, 4);
    Serial.println(" mm");

    Serial.println("[BME280]");
    Serial.print("  Temperatura:\t\t"); Serial.print(temp);  Serial.println(" *C");
    Serial.print("  Presion:\t\t");     Serial.print(pre);   Serial.println(" hPa");
    Serial.print("  Altitud aprox.:\t"); Serial.print(alt);  Serial.println(" m");
    Serial.print("  Humedad:\t\t");     Serial.print(hum);   Serial.println(" %");

    Serial.println("[LTR390]");
    Serial.print("  Luz visible (ALS):\t"); Serial.print(luxData); Serial.println(" Lux");
    Serial.print("  UV crudo:\t\t");        Serial.println(uvData);
    Serial.print("  Indice UV (OMS):\t");   Serial.println(uvi, 2);

    Serial.println("[SGP30]");
    Serial.print("  eCO2:\t\t\t"); Serial.print(co2);  Serial.println(" ppm");
    Serial.print("  TVOC:\t\t\t"); Serial.print(tvoc); Serial.println(" ppb");
    if (contadorCalentamiento < 15) {
      contadorCalentamiento++;
      Serial.print("  [Calentando... segundo ");
      Serial.print(contadorCalentamiento);
      Serial.println("/15]");
    }

    Serial.println("----------------------------------------------\n");

    // ════════════════════════════════════════════════════
    //  THINGSPEAK — envío cada POST_INTERVAL_MS (15 min)
    // ════════════════════════════════════════════════════
    if (millis() - lastPostMs >= POST_INTERVAL_MS) {

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Desconectado. Reintentando...");
        connectToWiFi();
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[ThingSpeak] Enviando datos...");
        bool ok = sendToThingSpeak(temp, hum, pre, co2, tvoc, lluviaPorHora, luxData);
        if (!ok) Serial.println("[Main] Falló el envío. Se reintentará en el próximo ciclo.");
      } else {
        Serial.println("[Main] Sin conexión WiFi. No se envió a ThingSpeak.");
      }

      lastPostMs = millis();
    }
  }

  delay(1000); // cadencia base 1 s (requerida por SGP30)
}