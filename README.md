# Sistema de Control de Acceso con Autenticación por Código (FreeRTOS)


## 📝 Descripción del Proyecto
Este proyecto implementa un sistema de seguridad y control de acceso automatizado utilizando FreeRTOS sobre el entorno ESP-IDF en C puro. El sistema administra de forma concurrente y en tiempo real el escaneo de un teclado matricial 4x4 físico, el procesamiento de una clave de seguridad, el manejo de bloqueos temporales por intentos fallidos, la emisión de alertas sonoras y la actualización de periféricos físicos de salida: una pantalla LCD 16x2 (mediante el bus de comunicación I2C con el chip PCF8574), indicadores LED de estado y un servomotor (SG90) que actúa como pestillo físico de la cerradura.

A diferencia de la programación secuencial tradicional basada en bucles bloqueantes (polling), este desarrollo utiliza un enfoque determinista estructurado en tareas con prioridades definidas. La comunicación entre subtareas se gestiona a través de colas de mensajes (Queue) y la protección de recursos compartidos de hardware se realiza mediante semáforos de exclusión mutua (Mutex).

El sistema destaca por su enfoque de ingeniería en dos pilares fundamentales:

Flexibilidad y modularidad de Hardware: Implementa un menú dinámico de configuración embebido (Kconfig.projbuild), el cual permite mapear y modificar gráficamente los pines GPIO de los periféricos (teclado, servos, LEDs, bus I2C) y la dirección hexadecimal de la pantalla (0x27) sin necesidad de alterar el código fuente.

Autonomía y Robustez Energética: El dispositivo fue diseñado y testeado para operar de forma 100% autónoma e independiente de una computadora, siendo alimentado por una fuente externa regulada de 5V DC a 2.4A (12W). Esto garantiza el suministro eléctrico necesario para absorber los picos de demanda de corriente del servomotor y el buzzer durante eventos críticos de acceso, evitando caídas de tensión (brownouts) y garantizando la estabilidad operativa del microcontrolador y el bus I2C.


## 📐 Arquitectura del Sistema (RTOS)

### 📊 Distribución de Recursos y Prioridades

La arquitectura de software de este sistema de control de acceso se basa en un modelo multitarea multitarea-cooperativa controlada por el planificador dinámico de FreeRTOS. El diseño se organiza en hilos de ejecución independientes (tareas) que se comunican de forma asíncrona mediante colas de mensajes y protegen los recursos críticos de hardware a través de semáforos, garantizando un comportamiento determinista.

A continuación, se detallan los componentes y recursos que estructuran el firmware:

| Recurso / Tarea | Tipo de Objeto | Prioridad | Stack Size | Descripción |
| :--- | :--- | :---: | :---: | :--- |
| **vTareaEscanearTeclado** | Tarea | 2 (Media) | 2048 bytes | Realiza el barrido matricial físico secuencial de las filas y columnas del teclado cada 40 ms. Aplica un algoritmo de *software debounce* (anti-rebote) para evitar lecturas duplicadas. |
| **vTareaProcesarSeguridad** | Tarea | 3 (Alta) | 2048 bytes | Procesa los caracteres recibidos, valida la coincidencia con la clave maestra, gestiona los tiempos de bloqueo dinámico y coordina las salidas físicas (Servomotor, LEDs, Buzzer y LCD). |
| **xColaTeclas** | Queue | — | 10 unidades | Almacena de forma segura hasta 10 datos de tipo `char` (`1-9`, `*`, `#`). Desacopla por completo la velocidad de entrada del usuario del procesamiento lógico de seguridad. |
| **xMutexLCD** | Mutex | — | — | Token de exclusión mutua que protege de forma atómica las funciones del bus I2C, evitando condiciones de carrera entre tareas que intenten escribir en la pantalla en el mismo instante. |


## ⚙️ Integración de Drivers de Hardware y Retrocompatibilidad
Un aspecto crítico en el diseño de esta arquitectura fue la migración e integración de componentes de hardware reales en un entorno de desarrollo moderno bajo ESP-IDF v5.5. Dado que los drivers comerciales para el controlador de pantalla I2C (PCF8574) suelen estar diseñados para versiones heredadas del framework, la arquitectura debió ser adaptada mediante técnicas de ingeniería de software:

Configuración Dinámica de Capa de Abstracción (Kconfig): Se integró un archivo descriptor Kconfig.projbuild acoplado al sistema de compilación CMake. Esto permite abstraer los números de pines GPIO físicos del teclado, el servomotor, los LEDs y las líneas del bus I2C (SDA/SCL), permitiendo su reconfiguración en tiempo de compilación a través de un entorno gráfico nativo (Menuconfig) sin alterar el código fuente.

Habilitación de Retrocompatibilidad del Kernel: Se configuró explícitamente la macro CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY en el núcleo de FreeRTOS para dar soporte a llamadas nativas eliminadas en las distribuciones modernas (como la macro de conversión temporal portTICK_RATE_MS utilizada internamente por el driver del LCD).

Parchado por Inyección de Macros: Para resolver conflictos con funciones de tiempo obsoletas del fabricante a nivel de la ROM profunda del microcontrolador, se inyectó una macro de sustitución en la compilación local (#define ets_delay_us(us) esp_rom_delay_us(us)), garantizando que las ráfagas de inicialización y el retardo de 4 bits exigidos por la pantalla se ejecuten bajo los estándares modernos de temporización de Espressif.


## 🔌 Hardware
El sistema se compone de una arquitectura de hardware basada en un microcontrolador de 32 bits y periféricos comerciales conectados mediante buses serie y líneas de propósito general (GPIO). La selección de los componentes responde a criterios de bajo consumo, viabilidad de integración en tiempo real y compatibilidad con lógica de 3.3V y 5V.

Los componentes de hardware integrados en el sistema son los siguientes:

Microcontrolador ESP32 (NodeMCU): Unidad central de procesamiento equipada con un microprocesador Xtensa de doble núcleo a 32 bits. Se encarga de la ejecución del kernel de FreeRTOS, el procesamiento del barrido matricial y el control de los actuadores mediante señales digitales y modulación por ancho de pulsos (PWM).

<img width="250" height="200" alt="esp32" src="https://github.com/user-attachments/assets/c0ac0a34-578e-43d8-aa60-1df34666bc11" />

Pantalla LCD 16x2 con Adaptador I2C (Chip PCF8574): Periférico de salida utilizado como interfaz visual para el usuario. El adaptador PCF8574 reduce el uso de pines en el microcontrolador a solo dos hilos (SDA y SCL) mediante el protocolo I2C, operando bajo la dirección hexadecimal 0x27. Cuenta con un potenciómetro acoplado para la regulación manual del contraste analógico.

<img width="250" height="200" alt="lcd 16x2" src="https://github.com/user-attachments/assets/7fda7b1a-fc5b-4f06-9e79-8a564a7a7827" />

Teclado Matricial 4x4: Periférico de entrada dispuesto en una matriz de 4 filas y 4 columnas. Permite la introducción de datos alfanuméricos mediante un proceso de barrido secuencial por hardware, donde las filas se configuran como salidas digitales y las columnas como entradas con resistencias de pull-up activadas.

<img width="250" height="200" alt="keypad 4x4" src="https://github.com/user-attachments/assets/6eca9232-2469-4911-babd-8106e6bb91b9" />

Servomotor SG90: Actuador analógico de posición utilizado como pestillo físico de la cerradura. Su posición se controla mediante señales PWM generadas por el controlador LEDC nativo del ESP32, variando el ancho de pulso entre 0.5 ms y 2.5 ms para definir el ángulo de giro (0 a 180 grados).

<img width="250" height="200" alt="servo sg90" src="https://github.com/user-attachments/assets/ef2994dc-00f2-47ab-9b91-79b46ae25731" />

Diodos LED (Verde y Rojo): Indicadores lumínicos de estado. El LED verde señaliza la condición de acceso permitido y la apertura del pestillo, mientras que el LED rojo indica estados de error en la clave o el bloqueo temporal del sistema por intentos fallidos.

<img width="250" height="200" alt="diodos leds" src="https://github.com/user-attachments/assets/c28283b2-f439-41d2-86a4-b1d092b47c46" />

Buzzer Pasivo/Activo: Transductor piezoeléctrico utilizado para la emisión de alertas sonoras y retroalimentación acústica ante la pulsación de teclas, accesos denegados o estados de alarma.

<img width="250" height="200" alt="buzzer pasivo-activo" src="https://github.com/user-attachments/assets/7bb1a06d-0b3f-440f-9b1c-969e9ad69651" />

Fuente de Alimentación Externa (5V DC, 2.4A, 12W): Unidad de potencia regulada conectada al puerto USB de la placa. Proporciona el flujo de corriente necesario para mitigar el ruido eléctrico y absorber las caídas de tensión provocadas por la activación simultánea del servomotor, los LEDs y la retroiluminación del LCD.

<img width="250" height="200" alt="Fuente de Alimentación Externa (5V DC, 2 4A, 12W)" src="https://github.com/user-attachments/assets/1f6a0d0c-702f-4584-9d98-d96039359474" />


## 📋 Requisitos
Para garantizar la correcta compilación, despliegue y operación del sistema de control de acceso, el entorno de desarrollo y ejecución debe cumplir con las siguientes especificaciones técnicas:

Requisitos de Software y Herramientas de Desarrollo:

Framework ESP-IDF (Versión 5.5 o superior): Entorno de desarrollo oficial de Espressif basado en herramientas GNU y CMake, necesario para la compilación del código fuente en C puro y la gestión de los componentes del proyecto.

Herramienta de Configuración Gráfica (Menuconfig / Kconfig): Extensión integrada en el entorno de desarrollo que permite interactuar con el archivo Kconfig.projbuild para la asignación dinámica de pines GPIO y parámetros del bus I2C en tiempo de compilación.

Sistema de Gestión de Componentes de ESP-IDF: Se requiere conectividad a internet durante la primera compilación para la descarga automática del componente externo de gestión del LCD (iamflinks/i2c_lcd_pcf8574) a través del registro oficial de Espressif.

Compilador CMake y Ninja: Herramientas del sistema de construcción de ESP-IDF utilizadas para procesar la estructura de archivos del proyecto y compilar el firmware de manera optimizada.

Requisitos de Configuración del Firmware:

Habilitación de Retrocompatibilidad en FreeRTOS: Es obligatorio tener activada la macro CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY en la configuración del proyecto para permitir la correcta ejecución de funciones de temporización heredadas en las librerías del LCD.

Configuración de Pines Libres de Conflicto: Los pines GPIO asignados en el menú de configuración para el teclado matricial deben estar completamente disjuntos de las líneas dedicadas al bus I2C (GPIO 21 para SDA y GPIO 22 para SCL), evitando superposiciones de hardware.

Requisitos Eléctricos y de Conexión Física:

Alimentación de Voltaje Dual Interno: El microcontrolador ESP32 debe operar con un voltaje regulado de 3.3V provisto por su placa de desarrollo, mientras que la pantalla LCD 16x2 y el servomotor SG90 deben conectarse a la línea de 5V (VIN/V5) para asegurar su correcto funcionamiento óptimo.

Resistencias de Pull-Up en el Bus I2C: En caso de longitudes de cable superiores a 15 centímetros en la protoboard, se requiere la adición de resistencias de pull-up externas (de 4.7 kOhm a 10 kOhm) conectadas a las líneas SDA y SCL para atenuar el ruido eléctrico en la comunicación de datos.

Masa Común de Referencia (GND): Todos los componentes del circuito (ESP32, LCD, Teclado y Servomotor) deben compartir un mismo nodo de tierra físico para evitar voltajes flotantes y errores de lectura en las señales analógicas y digitales.


## ⏱️ Conceptos de Tiempo Real Demostrados
Aquí tienes el último bloque correspondiente a los conceptos demostrados unificado en el mismo formato de texto plano y limpio, listo para que lo copies y pegues directamente en tu informe:

El diseño e implementación de este sistema de control de acceso sirve como demostración práctica de los principios fundamentales de los Sistemas Operativos de Tiempo Real (RTOS). A través del uso de las APIs nativas de FreeRTOS, se resolvieron problemas de concurrencia y sincronización de hardware que serían inviables mediante programación secuencial tradicional.

Los conceptos demostrados en el firmware se detallan a continuación:

Comunicación Asíncrona mediante Paso de Mensajes (Queues)
Implementado a través del objeto xColaTeclas. Este mecanismo funciona bajo un modelo de productor-consumidor estrictamente desacoplado:

Operación: La tarea vTareaEscanearTeclado (productor) realiza el barrido físico de los pines y, al detectar un pulso válido, deposita el carácter en la cola en cuestión de microsegundos mediante xQueueSend. La tarea vTareaProcesarSeguridad (consumidor) extrae los datos de forma secuencial.

Seguridad y Robustez: Durante los eventos de penalización (cuando el sistema bloquea el acceso durante 10 segundos por acumulación de intentos fallidos), la arquitectura utiliza la función xQueueReset. Esto permite purgar el buffer y descartar de forma segura cualquier pulsación fantasma o intento de sabotaje por teclado ocurrido durante el periodo de bloqueo, garantizando que el sistema no procese datos viejos al reactivarse.

Sincronización y Exclusión Mutua (Mutex)
Garantizado mediante el manejador xMutexLCD. En sistemas embebidos de tiempo real, el bus de comunicación física I2C es un recurso crítico propenso a condiciones de carrera (race conditions):

Operación: Al utilizar un driver físico compartido para la pantalla LCD, si la tarea encargada del escaneo y una futura tarea de telemetría o la misma tarea de seguridad intentaran escribir comandos en el bus I2C en el mismo instante, los datos binarios se corromperían en los hilos SDA/SCL (provocando la aparición de símbolos extraños o el congelamiento del hardware).

Solución Eficiente: El uso de xSemaphoreTake(xMutexLCD, portMAX_DELAY) actúa como un token atómico. La tarea de seguridad toma el control exclusivo del bus, realiza operaciones de limpieza (lcd_clear) o escritura (lcd_print) en el LCD, y libera el recurso de inmediato con xSemaphoreGive. Esto previene de forma absoluta la corrupción de datos en los periféricos de salida.

Sincronización por Bloqueo Eficiente y Determinismo
Demostrado mediante la configuración de tiempos de espera definidos por la macro portMAX_DELAY dentro del receptor de la cola:

Operación: La tarea de procesamiento de seguridad ejecuta la función xQueueReceive(&xColaTeclas, ..., portMAX_DELAY). En lugar de ejecutar un bucle de espera activa (busy waiting o polling) que consumiría recursos de cómputo de manera innecesaria, el planificador de FreeRTOS remueve la tarea de la lista de ejecución y la coloca en estado bloqueado (Blocked).

Optimización de Recursos: En estado de reposo (mientras nadie interactúa con el teclado físico), la tarea consume exactamente el 0% de tiempo de CPU. El microcontrolador queda libre para procesar otras subtareas de fondo o ingresar a modos de bajo consumo. En el instante exacto en que el usuario presiona una tecla, el kernel de FreeRTOS despierta la tarea de manera determinista en base a su prioridad, procesando el evento sin retardos perceptibles.

## 📋 Video

https://github.com/user-attachments/assets/6f5eab6c-8a36-40e3-8806-7e9917ee310c



