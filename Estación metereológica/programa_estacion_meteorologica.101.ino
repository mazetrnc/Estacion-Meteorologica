#include <WiFi.h>        // Manejo de WiFi del ESP32
#include <WiFiClient.h>  // Cliente TCP para conexiones HTTP
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SDA_PIN 26
#define SCL_PIN 27

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;

/********** CREDENCIALES DE RED **********/
const char* WIFI_SSID     = "SIDIALGA";      // <-- Cambia por el nombre de tu red WiFi
const char* WIFI_PASSWORD = "G0g0M3M0T4T4";  // <-- Cambia por la contraseña de tu WiFi

/********** CONFIGURACIÓN THINGSPEAK **********/
const char* THINGSPEAK_HOST = "api.thingspeak.com"; // Host del API de ThingSpeak
const int   THINGSPEAK_PORT = 80;                   // Puerto HTTP estándar
String      WRITE_API_KEY   = "G3HHRN4N0G8XNWBY";   // <-- Cambia por tu Write API Key

/********** TEMPORIZACIÓN **********/
// ThingSpeak permite 1 actualización cada 15 s como mínimo.
// Usamos 20 s para estar holgados (evita rechazos por límite de tasa).
const unsigned long POST_INTERVAL_MS = 300000;

// Tiempo máximo de espera para conectar a WiFi (en milisegundos)
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;

/********** UTILIDADES **********/
WiFiClient client;  // Cliente TCP para realizar la petición HTTP
unsigned long lastPostMs = 0;

/**
 * Conecta el ESP32 a la red WiFi y muestra estado por el monitor serial.
 * Implementa un timeout para no quedarse bloqueado indefinidamente.
 */
void connectToWiFi() {
  Serial.println();
  Serial.print("[WiFi] Conectando a: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);             // Modo estación (se conecta a un AP)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < WIFI_CONNECT_TIMEOUT_MS) {
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

/**
 * Envía temperatura y humedad a ThingSpeak mediante HTTP GET:
 *   GET /update?api_key=WRITE_API_KEY&field1=<temp>&field2=<hum>
 * Retorna true si la solicitud se envió correctamente (no necesariamente que
 * ThingSpeak la haya contado; para eso revisamos el código de respuesta).
 */
bool sendToThingSpeak(float temp, float hum, float pre/*, float vel_vie, float co2, float tvoc, float pluvi, float rad_uv*/) {
  // Construimos la ruta con parámetros (field1 y field2)
  String url = "/update?api_key=" + WRITE_API_KEY +
               "&field1=" + String(temp) +   // 1 decimal
               "&field2=" + String(hum) +
               "&field3=" + String(pre) +
               /*"&field4=" + String(vel_vie, 1) +
               "&field5=" + String(co2, 1) +
               "&field6=" + String(tvoc, 1);
               "&field7=" + String(pluvi, 1);
               "&field8=" + String(rad_uv, 1);*/

  Serial.print("[HTTP] Conectando a ");
  Serial.print(THINGSPEAK_HOST);
  Serial.print(":");
  Serial.println(THINGSPEAK_PORT);

  // Abrimos conexión TCP al host/puerto indicados
  if (!client.connect(THINGSPEAK_HOST, THINGSPEAK_PORT)) {
    Serial.println("[HTTP] Error: No se pudo abrir conexión TCP con ThingSpeak.");
    return false;
  }

  // Preparamos la petición HTTP GET siguiendo el protocolo HTTP/1.1
  String request =
      String("GET ") + url + " HTTP/1.1\r\n" +
      "Host: " + THINGSPEAK_HOST + "\r\n" +
      "Connection: close\r\n\r\n";

  // Enviamos la petición al servidor
  client.print(request);

  Serial.println("[HTTP] Petición enviada:");
  Serial.println(request);

  // Esperamos una respuesta (con un pequeño timeout manual)
  unsigned long startWait = millis();
  while (!client.available() && (millis() - startWait) < 5000) {
    delay(10);
  }

  // Si no hay datos del servidor, consideramos que falló la comunicación
  if (!client.available()) {
    Serial.println("[HTTP] Error: Sin respuesta del servidor.");
    client.stop();
    return false;
  }

  // Leemos la respuesta del servidor por completo y la mostramos
  // IMPORTANTE: ThingSpeak devuelve un cuerpo con un número (0 = rechazado / error,
  // >0 = ID de la entrada publicada).
  String response;
  while (client.available()) {
    String line = client.readStringUntil('\n');
    response += line + "\n";
  }
  client.stop();

  Serial.println("[HTTP] Respuesta de ThingSpeak:");
  Serial.println("--------------------------------");
  Serial.println(response);
  Serial.println("--------------------------------");

  // Extra sencilla verificación: buscamos el fin de cabeceras y leemos el cuerpo.
  // El cuerpo suele ser un número en la última línea; si es "0" no se contó.
  int lastNewline = response.lastIndexOf('\n', response.length() - 2);
  String body = response.substring(lastNewline + 1);
  body.trim();

  if (body == "0") {
    Serial.println("[ThingSpeak] Publicación rechazada (código 0). Verifica el intervalo (>15s) y tu API Key.");
    return false;
  }

  Serial.print("[ThingSpeak] Publicación exitosa. Entry ID: ");
  Serial.println(body);
  return true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!bme.begin(0x76)) {
    Serial.println("No se encontró un sensor BME280 válido, verifica los cables!");
    while (1);
  }
    // Conectamos a WiFi
  connectToWiFi();

  // Si no conectó, podemos seguir intentando más tarde en loop()
  lastPostMs = millis() - POST_INTERVAL_MS; // forzar primera lectura/envío inmediato
}

void loop() {
  // Si no hay WiFi, intentamos reconectar de forma no bloqueante
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Desconectado. Reintentando conexión...");
    connectToWiFi();
  }

  // Respetamos el intervalo mínimo antes de medir y publicar
  if (millis() - lastPostMs < POST_INTERVAL_MS) {
    delay(50);
    return;
  }

  Serial.print("Temperatura = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");

  Serial.print("Presión = ");
  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  Serial.print("Altitud aproximada = ");
  Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  Serial.println(" m");

  Serial.print("Humedad = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

  Serial.println();

    /********** ENVÍO A THINGSPEAK **********/
  if (WiFi.status() == WL_CONNECTED) {
    bool ok = sendToThingSpeak(bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0F/*, vel_viento, co2, tvoc, precipitacion, radiacion_uv*/);
    if (!ok) {
      Serial.println("[Main] Falló el envío. Se reintentará en el próximo ciclo.");
    }
  } else {
    Serial.println("[Main] Sin conexión WiFi. No se envió a ThingSpeak.");
  }

  // Actualizamos marca de tiempo del último intento de publicación
  lastPostMs = millis();
  delay(1000);
}   