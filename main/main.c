#include <stdio.h>
#include <string.h>

// Cabeceras nativas de FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// --- CABECERAS DE ROM REQUERIDAS PARA EL PARCHE ---
#include "esp_rom_sys.h"      // Contiene la definición de esp_rom_delay_us
#include "rom/ets_sys.h"      // Estructura base de tiempos de Espressif

// --- ¡EL PARCHE MÁGICO! ---

#define esp_rom_delay_us(us)
#define ets_delay_us  esp_rom_delay_us

// Ahora sí, incluimos la librería del LCD que se va a beneficiar del parche
#include "I2C_LCD_PCF8574.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"

// Mapeo unificado directo de variables de configuración de Kconfig
#define CLAVE_CORRECTA    CONFIG_CLAVE_MAESTRA
#define PIN_CERRADURA     CONFIG_PIN_SERVO
#define LED_VERDE         CONFIG_PIN_LED_VERDE
#define LED_ROJO          CONFIG_PIN_LED_ROJO
#define PIN_BUZZER        CONFIG_PIN_BUZZER

// Recursos de FreeRTOS
QueueHandle_t xColaTeclas = NULL;
SemaphoreHandle_t xMutexLCD = NULL;

// --- CONTROLADOR GLOBAL DE LA PANTALLA LCD ---
i2c_lcd_pcf8574_handle_t xLcdHandle;

// Prototipos de tareas
void vModificarAnguloServo(int angulo);
void vTareaEscanearTeclado(void *pvParameters);
void vTareaProcesarSeguridad(void *pvParameters);
void vBuzzerAccesoPermitido(void);
void vBuzzerAccesoDenegado(void);

void app_main(void) 
{
    printf("Inicializando Sistema de Seguridad RTOS en ESP32...\n");

    // 1. Configurar Teclado Matricial (Filas como salidas, Columnas como entradas con pull-up)
    uint64_t mascara_filas = (1ULL << CONFIG_PIN_FILA_1) | (1ULL << CONFIG_PIN_FILA_2) | 
                             (1ULL << CONFIG_PIN_FILA_3) | (1ULL << CONFIG_PIN_FILA_4);
    gpio_config_t io_conf_filas = 
    {
        .pin_bit_mask = mascara_filas,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_filas);

    uint64_t mascara_columnas = (1ULL << CONFIG_PIN_COL_1) | (1ULL << CONFIG_PIN_COL_2) | 
                                (1ULL << CONFIG_PIN_COL_3) | (1ULL << CONFIG_PIN_COL_4);
    gpio_config_t io_conf_columnas = 
    {
        .pin_bit_mask = mascara_columnas,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_columnas);

    // ====================================================================
    // INICIALIZACIÓN DE SALIDAS DIGITALES (LEDS Y BUZZER DESDE KCONFIG)
    // ====================================================================
    
    // Configurar LED Verde
    gpio_reset_pin(LED_VERDE);
    gpio_set_direction(LED_VERDE, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_VERDE, 0); 

    // Configurar LED Rojo
    gpio_reset_pin(LED_ROJO);
    gpio_set_direction(LED_ROJO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_ROJO, 0); 

    // Configurar Buzzer dinámico 
    gpio_reset_pin(CONFIG_PIN_BUZZER);
    gpio_set_direction(CONFIG_PIN_BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_PIN_BUZZER, 0);

    // ====================================================================
    // CONFIGURACIÓN DEL PERIFÉRICO PWM REAL PARA EL SERVOMOTOR
    // ====================================================================
    
    // 1. Configurar el Temporizador (Timer) del PWM
    ledc_timer_config_t ledc_timer = 
    {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT, // Resolución de 13 bits (0 a 8191)
        .freq_hz          = 50,                // Frecuencia estándar para servos (50 Hz)
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Configurar el Canal de Salida acoplado al Pin del Servo
    ledc_channel_config_t ledc_channel = 
    {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_CERRADURA,       // Usa la macro de tu CONFIG_PIN_SERVO (GPIO 12)
        .duty           = 0,                   // Arranca cerrado (0% duty)
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
    
    // Colocar el servo en posición inicial de bloqueo (0 grados) inmediatamente
    vModificarAnguloServo(0);

    // ====================================================================
    // INICIALIZACIÓN FÍSICA DEL BUS I2C Y LCD
    // ====================================================================
    
    // 1. Configurar el driver maestro I2C nativo de ESP-IDF
    i2c_config_t conf_i2c = 
    {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_PIN_SDA,         
        .scl_io_num = CONFIG_PIN_SCL,         
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000            // Velocidad estándar I2C (100 kHz)
    };

    i2c_param_config(I2C_NUM_0, &conf_i2c);
    i2c_driver_install(I2C_NUM_0, conf_i2c.mode, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(100)); // Espera de estabilidad

    // 2. Inicializar la pantalla con la firma exacta de tu librería:
    // Pide: (&manejador, direccion_i2c, puerto_i2c)
    lcd_init(&xLcdHandle, CONFIG_LCD_ADDR, I2C_NUM_0);

    //LE DAMOS LA ORDEN DE ARRANQUE A LA PANTALLA PARA QUE SE CONFIGURE SEGÚN SUS PROTOCOLOS INTERNOS
    lcd_begin(&xLcdHandle, 16, 2);

    lcd_set_backlight(&xLcdHandle, true);// Enciende la luz de fondo de forma explícita
    
    
    // Opcional: Si tu librería requiere un paso de encendido posterior
    lcd_clear(&xLcdHandle);
    lcd_set_cursor(&xLcdHandle, 0, 0);
    lcd_print(&xLcdHandle, " Ingrese Clave: ");

    // 3. Crear herramientas de sincronización de FreeRTOS
    xColaTeclas = xQueueCreate(10, sizeof(char));
    xMutexLCD = xSemaphoreCreateMutex();

    if (xColaTeclas != NULL && xMutexLCD != NULL) 
    {
        xTaskCreate(vTareaEscanearTeclado, "Teclado", 2048, NULL, 2, NULL);
        xTaskCreate(vTareaProcesarSeguridad, "Seguridad", 2048, NULL, 3, NULL);
    }
}

/* --- Implementación de Funciones Auxiliares --- */

// Función de capa de abstracción para el servomotor
void vModificarAnguloServo(int angulo) 
{
    // Mapeo matemático para pasar de ángulo (0-180) a Duty Cycle en microsegundos
    // 0° -> 0.5ms (500us) | 90° -> 1.5ms (1500us) | 180° -> 2.5ms (2500us)
    uint32_t duty_us = 500 + ((angulo * 2000) / 180);
    
    // Convertir microsegundos al valor binario del duty cycle (Resolución de 13 bits)
    // Fórmula: (duty_us * (2^13 - 1)) / Período_Total_en_us (20000us para 50Hz)
    uint32_t duty_registro = (duty_us * 8191) / 20000;
    
    // Aplicar el nuevo Duty Cycle al hardware del ESP32
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_registro);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    printf("[SERVO REAL] Ángulo modificado a %d grados (Duty binario: %lu).\n", angulo, duty_registro);
}

//Funciones de sonido para el BUZZER.

void vBuzzerAccesoPermitido(void) 
{
    // Un pitido único, limpio y rápido de éxito (150ms)
    gpio_set_level(PIN_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(PIN_BUZZER, 0);
}

void vBuzzerAccesoDenegado(void) 
{
    // Tres ráfagas lentas y espaciadas de error (Claramente perceptibles)
    for (int i = 0; i < 3; i++) 
    {
        gpio_set_level(PIN_BUZZER, 1);
        vTaskDelay(pdMS_TO_TICKS(300)); // 300 milisegundos encendido
        gpio_set_level(PIN_BUZZER, 0);
        vTaskDelay(pdMS_TO_TICKS(150)); // 150 milisegundos de silencio
    }
}


/* --- Implementación de Tareas --- */

void vTareaEscanearTeclado(void *pvParameters) 
{
    // Definimos la matriz con los caracteres físicos del teclado 4x4
    char teclas[4][4] = 
    {
        {'1','2','3','A'},
        {'4','5','6','B'},
        {'7','8','9','C'},
        {'*','0','#','D'}
    };

    // Arreglos con las macros de tus pines del Kconfig para recorrerlos con un ciclo for
    const gpio_num_t pines_filas[4] = 
    {
        CONFIG_PIN_FILA_1, CONFIG_PIN_FILA_2, CONFIG_PIN_FILA_3, CONFIG_PIN_FILA_4
    };
    const gpio_num_t pines_columnas[4] = 
    {
        CONFIG_PIN_COL_1, CONFIG_PIN_COL_2, CONFIG_PIN_COL_3, CONFIG_PIN_COL_4
    };

    for (;;) 
    {
        // ALGORITMO DE BARRIDO MATRICIAL
        for (int f = 0; f < 4; f++) 
        {
            // 1. Ponemos la fila actual en BAJO (0V)
            gpio_set_level(pines_filas[f], 0);
            vTaskDelay(pdMS_TO_TICKS(5)); // Mini retraso para que se estabilice el voltaje del pin

            for (int c = 0; c < 4; c++) 
            {
                // 2. Si la columna lee un '0', significa que el botón en esa intersección se presionó
                if (gpio_get_level(pines_columnas[c]) == 0) 
                {
                    char teclaDetectada = teclas[f][c];
                    
                    // 3. Enviamos el caracter detectado a la cola de FreeRTOS
                    xQueueSend(xColaTeclas, &teclaDetectada, 0);

                    // 4. ANTI-REBOTE (Debounce): Esperamos a que el usuario suelte el botón 
                    // para evitar que mande 50 asteriscos de un solo golpe.
                    while (gpio_get_level(pines_columnas[c]) == 0) 
                    {
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                }
            }
            // 5. Volvemos a dejar la fila en ALTO (3.3V) antes de pasar a la siguiente
            gpio_set_level(pines_filas[f], 1);
        }
        
        vTaskDelay(pdMS_TO_TICKS(30)); // Frecuencia de escaneo periódico general
    }
}

void vTareaProcesarSeguridad(void *pvParameters) 
{
    char teclaRecibida;
    char bufferClave[5] = {0}; 
    uint8_t indiceClave = 0;
    uint8_t intentosFallidos = 0;

    for (;;) 
    {
        if (xQueueReceive(xColaTeclas, &teclaRecibida, portMAX_DELAY) == pdPASS) 
        {
            
            if (teclaRecibida == '#') 
            {
                if (indiceClave > 0) 
                {
                    bufferClave[indiceClave] = '\0';

                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                    {
                        lcd_clear(&xLcdHandle);
                        lcd_set_cursor(&xLcdHandle, 0, 0);
                        lcd_print(&xLcdHandle, "Verificando...");
                        xSemaphoreGive(xMutexLCD);
                    }
                    vTaskDelay(pdMS_TO_TICKS(500)); 

                    // =========================================================
                    // 🟢 CASO 1: LA CLAVE ES CORRECTA (ACCESO PERMITIDO)
                    // =========================================================
                    if (strcmp(bufferClave, CLAVE_CORRECTA) == 0) 
                    {
                        // 1. Activar indicadores físicos inmediatos (LED Verde y Buzzer de éxito)
                        gpio_set_level(CONFIG_PIN_LED_VERDE, 1);

                        vBuzzerAccesoPermitido();

                        // 2. Mostrar mensajes en el LCD
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, "   ACCESO OK!   ");
                            lcd_set_cursor(&xLcdHandle, 0, 1);
                            lcd_print(&xLcdHandle, "   BIENVENIDO   ");
                            xSemaphoreGive(xMutexLCD);
                        }

                        // 3. Abrir el pestillo físico (Mover el servo a 90 grados)
                        
                        vModificarAnguloServo(90); 

                        intentosFallidos = 0;
                        
                        // 4. Esperar 4 segundos con la puerta abierta
                        vTaskDelay(pdMS_TO_TICKS(4000)); 

                        // 5. Cierre automático de seguridad (Cerrar servo y apagar LED)
                        vModificarAnguloServo(0);
                        gpio_set_level(CONFIG_PIN_LED_VERDE, 0);

                        // 6. Restaurar el mensaje inicial de la pantalla
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, " Ingrese Clave: ");
                            xSemaphoreGive(xMutexLCD);
                        }
                    } 
                    // =========================================================
                    // 🔴 CASO 2: LA CLAVE ES INCORRECTA (ACCESO DENEGADO)
                    // =========================================================
                    else 
                    {
                        intentosFallidos++;
                        
                        // 1. Activar indicadores físicos inmediatos (LED Rojo y Buzzer de error)
                        gpio_set_level(CONFIG_PIN_LED_ROJO, 1);

                        vBuzzerAccesoDenegado();

                        // 2. Mostrar error en la pantalla LCD
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, "CLAVE INCORRECTA");
                            xSemaphoreGive(xMutexLCD);
                        }
                        
                        // Mantener la alerta visual por el tiempo restante de la penalización visual
                        vTaskDelay(pdMS_TO_TICKS(1000)); 
                        
                        // Apagar el LED Rojo después de la alerta inicial
                        gpio_set_level(CONFIG_PIN_LED_ROJO, 0);

                        // 3. Evaluar si corresponde bloqueo total por acumulación de intentos
                        if (intentosFallidos >= 3) 
                        {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                lcd_clear(&xLcdHandle);
                                lcd_set_cursor(&xLcdHandle, 0, 0);
                                lcd_print(&xLcdHandle, "BLOQUEADO 10 SEG");
                                xSemaphoreGive(xMutexLCD);
                            }
                            
                            // Dejar el LED Rojo encendido de forma fija durante todo el bloqueo
                            gpio_set_level(CONFIG_PIN_LED_ROJO, 1);
                            
                            xQueueReset(xColaTeclas); 
                            vTaskDelay(pdMS_TO_TICKS(10000)); 
                            
                            gpio_set_level(CONFIG_PIN_LED_ROJO, 0);
                            intentosFallidos = 0; 

                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                lcd_clear(&xLcdHandle);
                                lcd_set_cursor(&xLcdHandle, 0, 0);
                                lcd_print(&xLcdHandle, " Ingrese Clave: ");
                                xSemaphoreGive(xMutexLCD);
                            }
                        } 
                        else 
                        {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                lcd_clear(&xLcdHandle);
                                lcd_set_cursor(&xLcdHandle, 0, 0);
                                lcd_print(&xLcdHandle, " Ingrese Clave: ");
                                xSemaphoreGive(xMutexLCD);
                            }
                        }
                    }
                    memset(bufferClave, 0, sizeof(bufferClave));
                    indiceClave = 0;
                }
            } 
            else if (teclaRecibida == '*') 
            {
                memset(bufferClave, 0, sizeof(bufferClave));
                indiceClave = 0;
                if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                {
                    lcd_clear(&xLcdHandle);
                    lcd_set_cursor(&xLcdHandle, 0, 0);
                    lcd_print(&xLcdHandle, " Entrada Borrada");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    lcd_clear(&xLcdHandle);
                    lcd_set_cursor(&xLcdHandle, 0, 0);
                    lcd_print(&xLcdHandle, " Ingrese Clave: ");
                    xSemaphoreGive(xMutexLCD);
                }
            } 
            else 
            {
                if (indiceClave < 4) 
                {
                    bufferClave[indiceClave] = teclaRecibida;
                    
                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                    {

                        /* Si queremos que se vean los numeros que ingresas en la pantalla LCD.
                        lcd_set_cursor(&xLcdHandle, indiceClave, 1);
                        char textoNumero[2] = {teclaRecibida, '\0'};
                        lcd_print(&xLcdHandle, textoNumero);*/

                        //Si queremos que aparezca un asterisco en la posición actual
                        lcd_set_cursor(&xLcdHandle, indiceClave, 1);
                        lcd_print(&xLcdHandle, "*");
                        
                        xSemaphoreGive(xMutexLCD);
                    }
                    indiceClave++;
                }
            }
        }
    }
}



