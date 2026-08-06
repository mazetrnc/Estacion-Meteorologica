[Monitoreo de Estación Meteorológica en ThingSpeak](https://thingspeak.mathworks.com/channels/3108150) 

<h3>Funcionamiento de la Estación Meteorológica</h3>

La estación meteorológica desarrollada utiliza una **ESP32 como unidad central de procesamiento**, encargada de adquirir, procesar y transmitir los datos obtenidos por diversos sensores ambientales. El sistema integra varios módulos conectados principalmente mediante el protocolo **I2C**, así como comunicación **UART/RS485**, lo que permite medir múltiples variables meteorológicas y ambientales en tiempo real.

En primer lugar, el sensor **BME280** mide tres variables fundamentales del clima: **temperatura, humedad relativa y presión atmosférica**. A partir de la presión también se calcula la **altitud aproximada** utilizando una presión de referencia al nivel del mar. Estos datos son obtenidos a través del bus I2C y se almacenan en variables para su posterior procesamiento.

<img width="330" height="330" alt="image" src="https://github.com/user-attachments/assets/c48b5643-6d03-4af7-b338-50f1a16b69ae" />

La calidad del aire se monitorea mediante el sensor **SGP30**, que proporciona estimaciones de **CO₂ equivalente (eCO₂)** y **compuestos orgánicos volátiles totales (TVOC)**. Para mejorar la precisión de las mediciones, el código calcula la **humedad absoluta** a partir de los datos del BME280 y la envía al SGP30 como parámetro de compensación ambiental.

<img width="330" height="330" alt="image" src="https://github.com/user-attachments/assets/62862a37-8d8c-4d81-96e4-63406ffce1f9" />

La **radiación ultravioleta** se mide con el sensor **LTR390**, también conectado por I2C, el cual proporciona un valor de radiación UV que puede relacionarse con el **índice UV utilizado por organismos de salud**.

<img width="267" height="250" alt="image" src="https://github.com/user-attachments/assets/ae9a894b-2cec-42ac-8f63-2014c343f8e3" />

Para medir la **precipitación**, se utiliza un **sensor de lluvia con cubeta basculante**, que contabiliza los pulsos generados por cada basculación causada por la acumulación de agua. El código convierte estos pulsos en **milímetros de lluvia acumulada**, permitiendo conocer la precipitación en un periodo determinado.

<img width="330" height="330" alt="image" src="https://github.com/user-attachments/assets/78c71bc4-185f-4e3a-b35a-1947aa20cd9f" />

La **velocidad del viento** se obtiene mediante un **anemómetro con salida RS485**, que transmite los datos usando el protocolo **Modbus RTU**. Debido a que la ESP32 trabaja con niveles TTL, se emplea un **módulo conversor MAX485** para adaptar la señal RS485 a comunicación serial. El código envía una solicitud Modbus al anemómetro, recibe la respuesta, verifica su integridad mediante **CRC16**, y finalmente convierte el valor recibido en **metros por segundo (m/s)**.

<img width="220" height="220" alt="image" src="https://github.com/user-attachments/assets/d7884215-2c5e-4f8c-a152-a38dfabb4dea" />
<img width="220" height="220" alt="image" src="https://github.com/user-attachments/assets/ff7984eb-23db-465c-ad59-f6c2fb85383c" />
Una vez que la ESP32 realiza la lectura de todos los sensores integrados (aproximadamente cada **5 segundos**), procesa la información y la expone mediante una arquitectura de red dual que combina monitoreo local e IoT en la nube simultáneamente:

1. **Servidor Web Local en Tiempo Real (Modo AP):** La ESP32 crea un punto de acceso Wi-Fi propio (`SSID: EstacionMeteo`) donde aloja un **servidor web asíncrono (`ESPAsyncWebServer`)** en la IP `192.168.4.1`. La interfaz web (almacenada en `PROGMEM` / `pagina_web.h`) consulta automáticamente el endpoint `/datos` mediante peticiones `XMLHttpRequest` en segundo plano **cada 10 segundos**. Esto actualiza los indicadores, alertas y gráficos locales de forma instantánea sin requerir recargas de página ni depender de una conexión a Internet.

2. **Publicación Remota en la Nube (Modo STA - ThingSpeak):** De forma paralela y utilizando la misma interfaz de red (`WIFI_AP_STA`), la ESP32 se conecta como cliente a un router local con salida a Internet. **Cada 5 minutos (`POST_INTERVAL_MS = 300000`)**, el microcontrolador realiza una **petición HTTP GET** al servidor de **ThingSpeak** (`api.thingspeak.com`), mapeando las lecturas a los campos del canal:

* **Field 1:** Temperatura (°C)

* **Field 2:** Humedad (%)

* **Field 3:** Presión (hPa)

* **Field 4:** Velocidad del viento (m/s)

* **Field 5:** eCO₂ (ppm)

* **Field 6:** TVOC (ppb)

* **Field 7:** Precipitación / Tasa (mm/h)

* **Field 8:** Índice UV (OMS)

Ambos modos funcionan simultáneamente y de forma totalmente independiente: si la conexión Wi-Fi a Internet falla o no está disponible, el envío a ThingSpeak se omite temporalmente sin afectar la operación del servidor web local, el cual sigue sirviendo los datos en tiempo real de forma ininterrumpida.

En conjunto, esta estación meteorológica integra sensores ambientales de precisión, protocolos de comunicación industrial y de bus (I2C y RS485/Modbus RTU), servidor web embebido y conectividad IoT híbrida para crear un sistema robusto capaz de **monitorear condiciones climáticas en tiempo real de forma local y, al mismo tiempo, publicar los resultados en una plataforma en la nube para su almacenamiento histórico y análisis remoto**.
