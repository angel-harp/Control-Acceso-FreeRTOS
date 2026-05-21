# Sistema de Control de Acceso con Autenticación por Código (FreeRTOS)

## 📌 Descripción del Proyecto
Este proyecto implementa un sistema de seguridad y control de acceso automatizado utilizando **FreeRTOS** en C puro. El sistema administra de forma concurrente el escaneo de un teclado matricial 4x4, el procesamiento de una clave de seguridad, el manejo de bloqueos por intentos fallidos y la actualización de una pantalla LCD 16x2 (I2C) junto con un servomotor (SG90) que actúa como pestillo físico.

A diferencia de la programación secuencial tradicional basada en bucles bloqueantes (*polling*), este desarrollo utiliza un enfoque determinista estructurado en tareas con prioridades definidas, colas de mensajes (`Queue`) y semáforos de exclusión mutua (`Mutex`).

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