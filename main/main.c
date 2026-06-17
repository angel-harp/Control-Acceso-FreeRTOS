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
// Cada vez que la librería intente usar ets_delay_us, usará la versión moderna nativa.
#define ets_delay_us(us) esp_rom_delay_us(us)

// Ahora sí, incluimos la librería del LCD que se va a beneficiar del parche
#include "I2C_LCD_PCF8574.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

// Mapeo de variables de configuración
#define CLAVE_CORRECTA   CONFIG_CLAVE_MAESTRA

// Definiciones de configuración (ver Menuconfig)
#define PIN_CERRADURA    CONFIG_PIN_SERVO
#define PIN_ALARMA       CONFIG_PIN_LED_ROJO
#define LED_VERDE        CONFIG_PIN_LED_VERDE


// Recursos de FreeRTOS
QueueHandle_t xColaTeclas = NULL;
SemaphoreHandle_t xMutexLCD = NULL;

// --- CONTROLADOR GLOBAL DE LA PANTALLA LCD ---
i2c_lcd_pcf8574_handle_t xLcdHandle;

// Prototipos de tareas
void vTareaEscanearTeclado(void *pvParameters);
void vTareaProcesarSeguridad(void *pvParameters);

void app_main(void) {
    printf("Inicializando Sistema de Seguridad RTOS en ESP32...\n");

    // 1. Configurar Teclado Matricial (Filas como salidas, Columnas como entradas con pull-up)
    uint64_t mascara_filas = (1ULL << CONFIG_PIN_FILA_1) | (1ULL << CONFIG_PIN_FILA_2) | 
                             (1ULL << CONFIG_PIN_FILA_3) | (1ULL << CONFIG_PIN_FILA_4);
    gpio_config_t io_conf_filas = {
        .pin_bit_mask = mascara_filas,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_filas);

    uint64_t mascara_columnas = (1ULL << CONFIG_PIN_COL_1) | (1ULL << CONFIG_PIN_COL_2) | 
                                (1ULL << CONFIG_PIN_COL_3) | (1ULL << CONFIG_PIN_COL_4);
    gpio_config_t io_conf_columnas = {
        .pin_bit_mask = mascara_columnas,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_columnas);

    // ====================================================================
    // INICIALIZACIÓN FÍSICA DEL BUS I2C Y LCD
    // ====================================================================
    
    // 1. Configurar el driver maestro I2C nativo de ESP-IDF
    i2c_config_t conf_i2c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_PIN_SDA,         // Pin asignado en tu Kconfig
        .scl_io_num = CONFIG_PIN_SCL,         // Pin asignado en tu Kconfig
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000            // Velocidad estándar I2C (100 kHz)
    };
    i2c_param_config(I2C_NUM_0, &conf_i2c);
    i2c_driver_install(I2C_NUM_0, conf_i2c.mode, 0, 0, 0);

    // 2. Inicializar la pantalla con la firma exacta de tu librería:
    // Pide: (&manejador, direccion_i2c, puerto_i2c)
    lcd_init(&xLcdHandle, CONFIG_LCD_ADDR, I2C_NUM_0);
    
    // Opcional: Si tu librería requiere un paso de encendido posterior
    lcd_clear(&xLcdHandle);
    lcd_set_cursor(&xLcdHandle, 0, 0);
    lcd_print(&xLcdHandle, " Ingrese Clave: ");

    // 3. Crear herramientas de sincronización de FreeRTOS
    xColaTeclas = xQueueCreate(10, sizeof(char));
    xMutexLCD = xSemaphoreCreateMutex();

    if (xColaTeclas != NULL && xMutexLCD != NULL) {
        xTaskCreate(vTareaEscanearTeclado, "Teclado", 2048, NULL, 2, NULL);
        xTaskCreate(vTareaProcesarSeguridad, "Seguridad", 2048, NULL, 3, NULL);
    }
}

/* --- Implementación de Tareas --- */

void vTareaEscanearTeclado(void *pvParameters) {
    int caracterLeido;
    for (;;) {
        caracterLeido = fgetc(stdin); // Mantenemos la simulación por terminal por ahora
        if (caracterLeido != '\n' && caracterLeido != '\r' && caracterLeido != EOF) {
            if ((caracterLeido >= '0' && caracterLeido <= '9') || caracterLeido == '#' || caracterLeido == '*') {
                char tecla = (char)caracterLeido;
                xQueueSend(xColaTeclas, &tecla, 0);
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void vTareaProcesarSeguridad(void *pvParameters) {
    char teclaRecibida;
    char bufferClave[5] = {0}; 
    uint8_t indiceClave = 0;
    uint8_t intentosFallidos = 0;

    for (;;) {
        if (xQueueReceive(xColaTeclas, &teclaRecibida, portMAX_DELAY) == pdPASS) {
            
            if (teclaRecibida == '#') {
                if (indiceClave > 0) {
                    bufferClave[indiceClave] = '\0';

                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                        lcd_clear(&xLcdHandle);
                        lcd_set_cursor(&xLcdHandle, 0, 0);
                        lcd_print(&xLcdHandle, "Verificando...");
                        xSemaphoreGive(xMutexLCD);
                    }
                    vTaskDelay(pdMS_TO_TICKS(500)); 

                    if (strcmp(bufferClave, CLAVE_CORRECTA) == 0) {
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, "ACCESO PERMITIDO");
                            xSemaphoreGive(xMutexLCD);
                        }
                        intentosFallidos = 0;
                        vTaskDelay(pdMS_TO_TICKS(4000)); 

                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, " Ingrese Clave: ");
                            xSemaphoreGive(xMutexLCD);
                        }
                    } 
                    else {
                        intentosFallidos++;
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                            lcd_clear(&xLcdHandle);
                            lcd_set_cursor(&xLcdHandle, 0, 0);
                            lcd_print(&xLcdHandle, "CLAVE INCORRECTA");
                            xSemaphoreGive(xMutexLCD);
                        }
                        vTaskDelay(pdMS_TO_TICKS(2000)); 

                        if (intentosFallidos >= 3) {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                                lcd_clear(&xLcdHandle);
                                lcd_set_cursor(&xLcdHandle, 0, 0);
                                lcd_print(&xLcdHandle, "BLOQUEADO 10 SEG");
                                xSemaphoreGive(xMutexLCD);
                            }
                            xQueueReset(xColaTeclas); 
                            vTaskDelay(pdMS_TO_TICKS(10000)); 
                            intentosFallidos = 0; 

                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                                lcd_clear(&xLcdHandle);
                                lcd_set_cursor(&xLcdHandle, 0, 0);
                                lcd_print(&xLcdHandle, " Ingrese Clave: ");
                                xSemaphoreGive(xMutexLCD);
                            }
                        } 
                        else {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
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
            else if (teclaRecibida == '*') {
                memset(bufferClave, 0, sizeof(bufferClave));
                indiceClave = 0;
                if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
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
            else {
                if (indiceClave < 4) {
                    bufferClave[indiceClave] = teclaRecibida;
                    
                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                        // Posiciona el asterisco dinámicamente en la fila 2 (índice 1)
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