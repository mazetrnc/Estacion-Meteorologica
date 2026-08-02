// Definir el pin digital donde conectaste el cable de la cubeta
const int pinPluviometro = 14; 

// Variables globales para el conteo de lluvia
volatile int conteoPulsos = 0;  
unsigned long ultimoTiempoRebote = 0;
const int tiempoAntiRebote = 200; // 200ms para evitar falsos positivos mecánicos

// Función que se ejecuta instantáneamente cada vez que cae la cubeta
void IRAM_ATTR contarVueltaCubeta() {
  unsigned long tiempoActual = millis();
  
  // Filtro digital anti-rebote (Debounce)
  if (tiempoActual - ultimoTiempoRebote > tiempoAntiRebote) {
    conteoPulsos++;
    ultimoTiempoRebote = tiempoActual;
  }
}

void setup() {
  Serial.begin(115200);
  
  // ACTIVAR PULLUP INTERNO: Reemplaza la resistencia que hacía el módulo Gravity
  pinMode(pinPluviometro, INPUT_PULLUP);
  
  // Configurar la interrupción por flanco de bajada (FALLING)
  attachInterrupt(digitalPinToInterrupt(pinPluviometro), contarVueltaCubeta, FALLING);
  
  Serial.println("Estación Meteorológica: Sensor directo configurado con éxito.");
}

void loop() {
  // Imprime los datos acumulados cada 10 segundos
  delay(5000); 
  
  // Resolución exacta del pluviómetro DFRobot original por cada pulso
  float mmLluviaAcumulada = conteoPulsos * 0.2794; 
  
  Serial.print("Vuelcos de cubeta: ");
  Serial.print(conteoPulsos);
  Serial.print(" | Precipitación Total: ");
  Serial.print(mmLluviaAcumulada, 4);
  Serial.println(" mm");
  
  // NOTA: Si quieres reiniciar el conteo cada 24 horas, 
  // solo debes hacer: conteoPulsos = 0; bajo tu lógica de tiempo.
}