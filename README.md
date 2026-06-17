# Sistema de Control de Acceso con Autenticación por Código (FreeRTOS)

## 📌 Descripción del Proyecto
Este proyecto implementa un sistema de seguridad y control de acceso automatizado utilizando FreeRTOS sobre el entorno ESP-IDF en C puro. El sistema administra de forma concurrente y en tiempo real el escaneo de un teclado matricial 4x4 físico, el procesamiento de una clave de seguridad, el manejo de bloqueos temporales por intentos fallidos, la emisión de alertas sonoras y la actualización de periféricos físicos de salida: una pantalla LCD 16x2 (mediante el bus de comunicación I2C con el chip PCF8574), indicadores LED de estado y un servomotor (SG90) que actúa como pestillo físico de la cerradura.

A diferencia de la programación secuencial tradicional basada en bucles bloqueantes (polling), este desarrollo utiliza un enfoque determinista estructurado en tareas con prioridades definidas. La comunicación entre subtareas se gestiona a través de colas de mensajes (Queue) y la protección de recursos compartidos de hardware se realiza mediante semáforos de exclusión mutua (Mutex).

El sistema destaca por su enfoque de ingeniería en dos pilares fundamentales:

Flexibilidad y modularidad de Hardware: Implementa un menú dinámico de configuración embebido (Kconfig.projbuild), el cual permite mapear y modificar gráficamente los pines GPIO de los periféricos (teclado, servos, LEDs, bus I2C) y la dirección hexadecimal de la pantalla (0x27) sin necesidad de alterar el código fuente.

Autonomía y Robustez Energética: El dispositivo fue diseñado y testeado para operar de forma 100% autónoma e independiente de una computadora, siendo alimentado por una fuente externa regulada de 5V DC a 2.4A (12W). Esto garantiza el suministro eléctrico necesario para absorber los picos de demanda de corriente del servomotor y el buzzer durante eventos críticos de acceso, evitando caídas de tensión (brownouts) y garantizando la estabilidad operativa del microcontrolador y el bus I2C.
---

## 📐 Arquitectura del Sistema (RTOS)

### 📊 Distribución de Recursos y Prioridades

| Recurso / Tarea | Tipo de Objeto | Prioridad | Stack Size | Descripción |
| :--- | :--- | :---: | :---: | :--- |
| `vTareaEscanearTeclado` | Tarea | 2 (Media) | 2048 bytes | Realiza el escaneo periódico del teclado cada 40ms. |
| `vTareaProcesarSeguridad`| Tarea | 3 (Alta) | 2048 bytes | Procesa los dígitos de la cola, valida la clave y controla los actuadores (LEDs, Servo y LCD). |
| `xColaTeclas` | Queue | - | - | Almacena hasta 10 caracteres (`char`) para desacoplar la entrada del usuario del procesamiento. |
| `xMutexLCD` | Mutex | - | - | Garantiza la exclusión mutua sobre el bus I2C para evitar corrupción de datos en la pantalla. |

---

## 🚀 Conceptos de Tiempo Real Demostrados

1. **Paso de Mensajes Asíncrono (`Queue`):** La tarea del teclado produce pulsaciones que la tarea de seguridad consume. Si el sistema se bloquea por penalización, la cola almacena o descarta caracteres de forma segura.
2. **Exclusión Mutua (`Mutex`):** Protege el bus de comunicación compartido (I2C) de la pantalla LCD, evitando colisiones de datos si múltiples tareas intentan escribir en ella en el futuro.
3. **Bloqueo Eficiente (`portMAX_DELAY`):** La tarea de procesamiento de código consume **0% de CPU** en estado de reposo, permaneciendo bloqueada hasta que ingresa un evento a la cola.
