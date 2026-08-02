#include <Wire.h>
#include <Adafruit_LTR390.h>

// Declaración global del sensor para que esté disponible en todo el sketch
Adafruit_LTR390 ltr = Adafruit_LTR390();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("Iniciando sensor LTR390...");
  
  // Configuración de los pines I2C específicos para tu ESP32 (SDA=32, SCL=33)
  Wire.begin(32, 33);
  
  if (!ltr.begin(&Wire)) {
    Serial.println("¡No se encontró el sensor LTR390! Revisa el cableado.");
    while (1) delay(10);
  }
  Serial.println("Sensor encontrado correctamente.");

  // Configuración inicial de ganancia y resolución
  ltr.setGain(LTR390_GAIN_18);  // Ganancia x18 para medición UV estándar
  ltr.setResolution(LTR390_RESOLUTION_16BIT);
}

void loop() {
  // 1. Configurar modo UV y leer datos
  ltr.setMode(LTR390_MODE_UVS);
  delay(100); // Pequeña pausa para que el sensor cambie de modo internamente
  
  if (ltr.newDataAvailable()) {
    uint32_t uvData = ltr.readUVS();
    // Cálculo del Índice UV basado en ganancia de 18x y resolución de 16 bits
    float uvi = (float)uvData / 2300.0;

    // 2. Configurar modo Luz Visible (ALS) y leer datos
    ltr.setMode(LTR390_MODE_ALS);
    delay(100); // Pausa para estabilizar el cambio de modo
    uint32_t alsData = ltr.readALS();
    // Conversión aproximada a Lux
    float luxData = alsData * 0.6; 

    // Imprimir los resultados unificados en el Monitor Serie
    Serial.print("Luz Visible (ALS): ");
    Serial.print(luxData);
    Serial.print(" Lux | ");
    
    Serial.print("Datos UV Crudos: ");
    Serial.print(uvData);
    
    Serial.print(" | Índice UV (OMS): ");
    Serial.println(uvi, 2);
  }
  
  delay(2000); // Espera 2 segundos antes de la siguiente muestra completa
}
