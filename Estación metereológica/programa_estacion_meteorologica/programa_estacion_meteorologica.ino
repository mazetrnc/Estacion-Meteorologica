#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_LTR390.h>
#include "DFRobot_RainfallSensor.h"
#include <SoftwareSerial.h>

float temperatura = 0.0;
float humedad = 0.0;
float presion = 0.0;
float altitud = 0.0;
float vel_viento = 0.0;
float co2 = 0.0;
int tvoc = 0.0;
float precipitacion = 0.0;
float radiacion_uv = 0.0;

TwoWire I2C1 = TwoWire(0);
TwoWire I2C2 = TwoWire(1);
TwonWire I2C3 = TwoWire(2);

#define SEALEVELPRESSURE_HPA (1013.25) // Presión al nivel del mar en hPa

Adafruit_BME280 bme;
//
Adafruit_LTR390 ltr = Adafruit_LTR390();
//
DFRobot_RainfallSensor_I2C Sensor(&Wire);
//
Adafruit_SGP30 sgp;

uint32_t getAbsoluteHumidity(float temperature, float humidity) {
  const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature));
  return static_cast<uint32_t>(1000.0f * absoluteHumidity); // mg/m³
}
//
#define DE_PIN 3
#define RE_PIN 2
#define RS485_RX 8
#define RS485_TX 9

SoftwareSerial modbusSerial(RS485_RX, RS485_TX); // RX, TX

uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B}; // Read 2 registers from address 0

void preTransmission() {
 digitalWrite(DE_PIN, HIGH);
 digitalWrite(RE_PIN, HIGH);
}

void postTransmission() {
 digitalWrite(DE_PIN, LOW);
 digitalWrite(RE_PIN, LOW);
}

void sendRequest() {
 preTransmission();
 for (int i = 0; i < sizeof(request); i++) {
  modbusSerial.write(request[i]);
 }
 modbusSerial.flush(); // Wait for transmission to finish
 postTransmission();
}

uint16_t crc16(uint8_t *buf, uint8_t len) {
 uint16_t crc = 0xFFFF;
 for (int pos = 0; pos < len; pos++) {
  crc ^= (uint16_t)buf[pos];
  for (int i = 0; i < 8; i++) {
   if (crc & 1) {
    crc = (crc >> 1) ^ 0xA001;
   } else {
    crc = crc >> 1;
   }
  }
 }
 return crc;
}

void writeToModule(String content) {
 char* bufc = (char*) malloc(sizeof(char) * content.length() + 1);
 content.toCharArray(bufc, content.length() + 1);
 netSerial.write(bufc);
 free(bufc);
 delay(100);
}

void setup() {
  Serial.begin(115200);
  delay(100);
    // Initialize Bus 1 (Pins 21, 22) - BME280
  I2C1.begin(21, 22, 400000); 
  // Initialize Bus 2 (Pins 17, 16) - SGP30
  I2C2.begin(17, 16, 400000); 
  // Initialize Bus 2 (Pins 17, 16) - LTR390
  I2C3.begin(14, 12, 400000); 

  if (!bme.begin(0x76)) { //bme280
    Serial.println("No se encontró un sensor BME280 válido, verifica los cables.");
    while (1);
  }
  bme.sea_level_pressure = SEALEVELPRESSURE_HPA;
///
 pinMode(DE_PIN, OUTPUT); //anemometro
 pinMode(RE_PIN, OUTPUT);
 postTransmission();
 while (!Serial);
 Serial.println("Started");
 modbusSerial.begin(9600);
///
  Serial.println("Adafruit LTR-390 test"); //LTR-390
  if (!ltr.begin()) {
    Serial.println("Couldn't find LTR sensor!");
    while (1) delay(10);
  }
  Serial.println("Found LTR sensor!");
  ltr.setMode(LTR390_MODE_UVS);
  ////
    delay(1000); //pluviometro
  while(!Sensor.begin()) {
    Serial.println("Sensor init err!!!");
    delay(1000);
  }
  Serial.print("vid:\t");
  Serial.println(Sensor.vid, HEX);
  Serial.print("pid:\t");
  Serial.println(Sensor.pid, HEX);
  Serial.print("Version:\t");
  Serial.println(Sensor.getFirmwareVersion());
  ///
  while (!Serial) delay(10); //SGP30

  if (!sgp.begin()) {
    Serial.println("Sensor SGP30 not found. Check wiring!");
    while (1);
  }
  Serial.println("SGP30 initialized successfully.");
}
}

void loop() {

float temperatura = bme.readTemperature();
float humedad = bme.readHumidity();
float presion = bme.readPressure() / 100.0F;
float altitud = bme.readAltitude(SEALEVELPRESSURE_HPA);
float vel_viento = windSpeed_mps;
float co2 = sgp.eCO2;
int tvoc = sgp.TVOC;
float precipitacion = Sensor.getRainfall(12);
float radiacion_uv = ltr.readUVS();

sgp.setHumidity(getAbsoluteHumidity(temperatura, humedad)); // Set humidity compensation

 sendRequest();
 //delay(100); // Wait for sensor response

 uint8_t response[9];
 int bytesRead = 0;
 unsigned long startTime = millis();

 while (bytesRead < 9 && millis() - startTime < 200) {
  if (modbusSerial.available()) {
   response[bytesRead++] = modbusSerial.read();
  }
 }

 if (bytesRead == 9) {
  uint16_t crc_calc = crc16(response, 7);
  uint16_t crc_recv = response[7] | (response[8] << 8);

  if (crc_calc == crc_recv) {
   uint16_t reg0 = (response[3] << 8) | response[4];
   uint16_t reg1 = (response[5] << 8) | response[6];

   float windSpeed_mps = reg0 * 0.1;
/*
   Serial.print("Wind speed raw: ");
   Serial.print(reg0);
   Serial.print(" | Wind speed mps: ");
   Serial.print(windSpeed_mps);
   Serial.print(" | Wind level: ");
   Serial.println(reg1);

   delay(100);
*/
  } else {
   Serial.println("CRC mismatch");
  }
 } else {
  Serial.println("No response or incomplete frame");
 }
  // Imprimir para depuración
  Serial.println(F("-----------------------------"));
  Serial.print(F("Temperatura: ")); Serial.print(temperatura); Serial.println(F(" °C"));
  Serial.print(F("Humedad: ")); Serial.print(humedad); Serial.println(F("%"));
  Serial.print(F("Presión: ")); Serial.print(presion); Serial.println(F(" hPa"));
  Serial.print(F("Altitud: ")); Serial.print(altitud); Serial.println(F(" m"));
  Serial.print(F("Velocidad del viento: ")); Serial.print(vel_viento); Serial.println(F("m/s"));
  if (sgp.IAQmeasure()) {
    Serial.print(F("Niveles de eCO2: ")); Serial.print(co2); Serial.println(F(" ppm"));
    Serial.print(F("Niveles de TVOC: ")); Serial.print(tvoc); Serial.println(F("ppb"));
  } else {
    Serial.println("Measurement failed!");
  }
  Serial.print(F("Precipitación Pluvial 12H: ")); Serial.print(precipitacion); Serial.println(F("mm"));
  if (ltr.newDataAvailable()) {
    Serial.print(F("Radiación UV: ")); Serial.print(radiacion_uv); Serial.println(F("Indice OMS"));
  }
  delay(1000)
}