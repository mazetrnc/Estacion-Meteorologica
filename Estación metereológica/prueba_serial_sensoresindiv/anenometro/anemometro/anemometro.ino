/**
 * Anemómetro RS485 Modbus RTU - ESP32
 * Librería: ModbusMaster (instalar desde gestor de librerías)
 *
 * Conexiones MAX485 <-> ESP32:
 *   RO  (Receiver Output)  --> GPIO 16 (RX2)
 *   DI  (Driver Input)     --> GPIO 17 (TX2)
 *   DE                     --> GPIO 25
 *   RE                     --> GPIO 26
 *   VCC                    --> 5V
 *   GND                    --> GND
 *
 * Sensor <-> MAX485:
 *   Rojo    (Power +)    --> 12-24V fuente externa
 *   Negro   (Power -)    --> GND compartido
 *   Amarillo (RS485 A)   --> A del MAX485
 *   Verde   (RS485 B)    --> B del MAX485
 */

#include <HardwareSerial.h>
#include <ModbusMaster.h>

#define RX_PIN   16
#define TX_PIN   17
#define DE_PIN   25
#define RE_PIN   26

#define MODBUS_ADDRESS  3

HardwareSerial RS485Serial(2);
ModbusMaster node;

// ── Callbacks para controlar DE y RE por separado ──────
void preTransmission() {
  digitalWrite(RE_PIN, HIGH); // RE HIGH = deshabilitar receptor
  digitalWrite(DE_PIN, HIGH); // DE HIGH = habilitar driver (TX)
}

void postTransmission() {
  digitalWrite(DE_PIN, LOW);  // DE LOW = deshabilitar driver
  digitalWrite(RE_PIN, LOW);  // RE LOW = habilitar receptor (RX)
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Anemómetro RS485 ModbusMaster - ESP32 ===\n");

  // Configurar pines DE y RE
  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, LOW); // Modo recepción por defecto

  // Iniciar UART2
  RS485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(100);

  // Configurar ModbusMaster
  node.begin(MODBUS_ADDRESS, RS485Serial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Listo. Leyendo cada 2 segundos...\n");
}

void loop() {
  // Leer 1 registro desde 0x0000 (velocidad del viento)
  uint8_t result = node.readHoldingRegisters(0x0000, 1);

  if (result == node.ku8MBSuccess) {
    uint16_t raw = node.getResponseBuffer(0);
    float ms    = raw / 10.0;
    float kmh   = ms * 3.6;
    float knots = ms * 1.94384;

    Serial.println("──────────────────────────────");
    Serial.print("Raw     : "); Serial.println(raw);
    Serial.print("m/s     : "); Serial.println(ms, 1);
    Serial.print("km/h    : "); Serial.println(kmh, 1);
    Serial.print("nudos   : "); Serial.println(knots, 1);
    Serial.println("──────────────────────────────");
  } else {
    Serial.print("[ERROR] Código: 0x");
    Serial.println(result, HEX);
    // Códigos comunes:
    // 0xE1 = timeout (sin respuesta)
    // 0xE2 = error en datos recibidos
    // 0xE3 = CRC incorrecto
  }

  delay(2000);
}