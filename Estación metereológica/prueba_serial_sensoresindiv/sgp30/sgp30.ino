#include <Wire.h>
#include "Adafruit_SGP30.h"

Adafruit_SGP30 sgp;
int contadorCalentamiento = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // Espera a que se abra el monitor serie

  Serial.println("\n--- Iniciando Prueba SGP30 con ESP32 ---");

  // Configura pines personalizados para ESP32: SDA (32) y SCL (33)
  Wire.begin(32, 33);

  // Inicializa el sensor en el bus I2C configurado
  if (!sgp.begin()) {
    Serial.println("¡Error! Sensor SGP30 no encontrado. Verifica conexiones.");
    while (1); // Detiene la ejecución si hay error
  }
  
  Serial.println("Sensor SGP30 detectado correctamente.");
}

void loop() {
  // CORRECCIÓN: Se usa IAQmeasure() en lugar de aqiMeasurements()
  if (!sgp.IAQmeasure()) {
    Serial.println("Error al leer los datos del sensor.");
    return;
  }
  
  // Muestra los datos en el Monitor Serie
  Serial.print("eCO2: "); 
  Serial.print(sgp.eCO2); 
  Serial.print(" ppm\t");
  
  Serial.print("TVOC: "); 
  Serial.print(sgp.TVOC); 
  Serial.println(" ppb");

  // Mensaje informativo durante el calentamiento del sensor (primeros 15 segundos)
  if (contadorCalentamiento < 15) {
    contadorCalentamiento++;
    Serial.print("[Calentando sensor... Segundo: ");
    Serial.print(contadorCalentamiento);
    Serial.println("/15]");
  }

  delay(1000); // Muestreo cada 1 segundo (recomendado por el fabricante)
}
