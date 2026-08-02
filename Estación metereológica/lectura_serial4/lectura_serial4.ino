// ============================================================
//  ESTACIÓN METEOROLÓGICA UNIFICADA
//  Sensores: BME280 | LTR390 | SGP30 | Pluviómetro Basculante
// ============================================================
//
//  Bus I2C  0 → Wire  (SDA=26, SCL=27)  →  BME280
//  Bus I2C  1 → Wire1 (SDA=32, SCL=33)  →  LTR390 + SGP30
//  Digital    → Pin 14 (INPUT_PULLUP)    →  Pluviómetro basculante
//
// ============================================================

#include <Wire.h>

// ── BME280 ────────────────────────────────────────────────
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#define SDA_BME   26
#define SCL_BME   27
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

// ── LTR390 ───────────────────────────────────────────────
#include <Adafruit_LTR390.h>
#define SDA_I2C1  32
#define SCL_I2C1  33
Adafruit_LTR390 ltr = Adafruit_LTR390();

// ── SGP30 ────────────────────────────────────────────────
#include "Adafruit_SGP30.h"
Adafruit_SGP30 sgp;
int contadorCalentamiento = 0;

// ── PLUVIÓMETRO BASCULANTE ────────────────────────────────
const int pinPluviometro = 14;
volatile int conteoPulsos = 0;
unsigned long ultimoTiempoRebote = 0;
const int tiempoAntiRebote = 200;          // ms anti-rebote mecánico

// Resolución original DFRobot: 0.2794 mm por vuelco de cubeta
const float MM_POR_PULSO = 0.2794;

// Tiempo de operación (para equivalente a getSensorWorkingTime)
unsigned long tiempoInicioMs = 0;

// ── TEMPORIZADOR LECTURA ──────────────────────────────────
unsigned long ultimaLectura = 0;
const unsigned long INTERVALO_MS = 5000;   // lectura cada 5 s


// ─────────────────────────────────────────────────────────
//  ISR: Interrupción del pluviómetro
// ─────────────────────────────────────────────────────────
void IRAM_ATTR contarVueltaCubeta() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimoTiempoRebote > tiempoAntiRebote) {
    conteoPulsos++;
    ultimoTiempoRebote = tiempoActual;
  }
}


// ─────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("\n==============================================");
  Serial.println("   ESTACIÓN METEOROLÓGICA - Iniciando...");
  Serial.println("==============================================\n");

  // ── Bus I2C 0: BME280 ─────────────────────────────────
  Wire.begin(SDA_BME, SCL_BME);
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] BME280 no encontrado en SDA=26 SCL=27. Verifica cables.");
    while (1);
  }
  Serial.println("[OK] BME280 detectado correctamente.");

  // ── Bus I2C 1: LTR390 + SGP30 ────────────────────────
  Wire1.begin(SDA_I2C1, SCL_I2C1);

  if (!ltr.begin(&Wire1)) {
    Serial.println("[ERROR] LTR390 no encontrado en SDA=32 SCL=33. Verifica cables.");
    while (1);
  }
  ltr.setGain(LTR390_GAIN_18);
  ltr.setResolution(LTR390_RESOLUTION_16BIT);
  Serial.println("[OK] LTR390 detectado correctamente.");

  if (!sgp.begin(&Wire1)) {
    Serial.println("[ERROR] SGP30 no encontrado en SDA=32 SCL=33. Verifica cables.");
    while (1);
  }
  Serial.println("[OK] SGP30 detectado correctamente.");

  // ── Pluviómetro basculante ────────────────────────────
  pinMode(pinPluviometro, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinPluviometro), contarVueltaCubeta, FALLING);
  tiempoInicioMs = millis();
  Serial.println("[OK] Pluviómetro basculante configurado en Pin 14.");

  Serial.println("\n==============================================");
  Serial.println("   Todos los sensores listos. Iniciando...");
  Serial.println("==============================================\n");

  ultimaLectura = millis();
}


// ─────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────
void loop() {

  // ── SGP30: lectura recomendada cada 1 segundo ─────────
  if (!sgp.IAQmeasure()) {
    Serial.println("[SGP30] Error al leer los datos del sensor.");
  }

  // ── Bloque de impresión cada INTERVALO_MS ─────────────
  unsigned long ahora = millis();
  if (ahora - ultimaLectura >= INTERVALO_MS) {
    ultimaLectura = ahora;

    Serial.println("----------------------------------------------");

    // ── 1. PLUVIÓMETRO BASCULANTE ─────────────────────
    float horasOperacion = (float)(millis() - tiempoInicioMs) / 3600000.0;
    float mmAcumulados   = conteoPulsos * MM_POR_PULSO;

    // Lluvia en la última hora (pulsos ocurridos; sin módulo Gravity
    // no hay buffer histórico, se muestra el acumulado total como referencia)
    float lluviaTotalMm  = mmAcumulados;

    Serial.println("[PLUVIÓMETRO BASCULANTE]");
    Serial.print("  Tiempo de operacion:\t");
    Serial.print(horasOperacion, 4);
    Serial.println(" H");

    Serial.print("  Vuelcos de cubeta:\t");
    Serial.println(conteoPulsos);

    Serial.print("  Precipitacion total:\t");
    Serial.print(lluviaTotalMm, 4);
    Serial.println(" mm");

    // Nota: sin el módulo Gravity no hay buffer horario interno;
    // se calcula la tasa promedio desde el inicio como aproximación.
    float lluviaPorHora = (horasOperacion > 0) ? (lluviaTotalMm / horasOperacion) : 0.0;
    Serial.print("  Tasa (~1 H equiv.):\t");
    Serial.print(lluviaPorHora, 4);
    Serial.println(" mm");

    // ── 2. BME280 ─────────────────────────────────────
    Serial.println("[BME280]");
    Serial.print("  Temperatura:\t\t");
    Serial.print(bme.readTemperature());
    Serial.println(" *C");

    Serial.print("  Presion:\t\t");
    Serial.print(bme.readPressure() / 100.0F);
    Serial.println(" hPa");

    Serial.print("  Altitud aprox.:\t");
    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.println(" m");

    Serial.print("  Humedad:\t\t");
    Serial.print(bme.readHumidity());
    Serial.println(" %");

    // ── 3. LTR390 ─────────────────────────────────────
    Serial.println("[LTR390]");

    // Modo UV
    ltr.setMode(LTR390_MODE_UVS);
    delay(100);
    if (ltr.newDataAvailable()) {
      uint32_t uvData = ltr.readUVS();
      float uvi = (float)uvData / 2300.0;

      // Modo ALS (luz visible)
      ltr.setMode(LTR390_MODE_ALS);
      delay(100);
      uint32_t alsData = ltr.readALS();
      float luxData = alsData * 0.6;

      Serial.print("  Luz visible (ALS):\t");
      Serial.print(luxData);
      Serial.println(" Lux");

      Serial.print("  UV crudo:\t\t");
      Serial.println(uvData);

      Serial.print("  Indice UV (OMS):\t");
      Serial.println(uvi, 2);
    } else {
      Serial.println("  [LTR390] Sin datos nuevos disponibles.");
    }

    // ── 4. SGP30 ──────────────────────────────────────
    Serial.println("[SGP30]");
    Serial.print("  eCO2:\t\t\t");
    Serial.print(sgp.eCO2);
    Serial.println(" ppm");

    Serial.print("  TVOC:\t\t\t");
    Serial.print(sgp.TVOC);
    Serial.println(" ppb");

    if (contadorCalentamiento < 15) {
      contadorCalentamiento++;
      Serial.print("  [Calentando... segundo ");
      Serial.print(contadorCalentamiento);
      Serial.println("/15]");
    }

    Serial.println("----------------------------------------------\n");
  }

  delay(1000); // Cadencia base 1 s (recomendada por SGP30)
}