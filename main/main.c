#include <stdio.h>
#include <string.h>

// Cabeceras nativas de FreeRTOS para ESP-IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Definiciones de configuración
#define CLAVE_CORRECTA "1234"

// Recursos de FreeRTOS
QueueHandle_t xColaTeclas = NULL;
SemaphoreHandle_t xMutexLCD = NULL;

// Prototipos de tareas
void vTareaEscanearTeclado(void *pvParameters);
void vTareaProcesarSeguridad(void *pvParameters);

// --- SOLUCIÓN: Punto de entrada oficial para ESP-IDF ---
void app_main(void) 
{
    printf("Inicializando Sistema de Seguridad RTOS en ESP32...\n");

    // Crear herramientas de sincronización
    xColaTeclas = xQueueCreate(10, sizeof(char));
    xMutexLCD = xSemaphoreCreateMutex();

    if (xColaTeclas != NULL && xMutexLCD != NULL) 
    {
        // Crear tareas concurrentes
        // En ESP-IDF, xTaskCreate recibe un parámetro extra al final (Task Handle), usamos NULL.
        xTaskCreate(vTareaEscanearTeclado, "Teclado", 2048, NULL, 2, NULL);
        xTaskCreate(vTareaProcesarSeguridad, "Seguridad", 2048, NULL, 3, NULL);
        
        // NOTA RELEVANTE: vTaskStartScheduler() NO se coloca en ESP-IDF.
        // El SDK ya inicia el planificador automáticamente antes de llamar a app_main.
    }
}

/* --- Implementación de Tareas --- */

void vTareaEscanearTeclado(void *pvParameters) 
{
    int caracterLeido; // <-- SOLUCIÓN: Cambiado a 'int' para resolver la advertencia de EOF (-1)

    for (;;) 
    {
        // Lee la consola estándar mapeada por el chip (UART/USB)
        caracterLeido = fgetc(stdin);

        if (caracterLeido != '\n' && caracterLeido != '\r' && caracterLeido != EOF) 
        {
            
            if ((caracterLeido >= '0' && caracterLeido <= '9') || caracterLeido == '#' || caracterLeido == '*') 
            {
                char tecla = (char)caracterLeido;
                // Enviamos la tecla detectada a la cola de forma segura
                xQueueSend(xColaTeclas, &tecla, 0);
            } 
            else 
            {
                if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                {
                    printf("\n[ERROR] Tecla '%c' invalida. Use solo numeros, * o #.\n", caracterLeido);
                    printf("[LCD] Ingrese Clave:\n");
                    xSemaphoreGive(xMutexLCD);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40)); // Muestreo cada 40ms
    }
}

void vTareaProcesarSeguridad(void *pvParameters) 
{
    char teclaRecibida;
    char bufferClave[5] = {0}; // Almacena los 4 dígitos + '\0'
    uint8_t indiceClave = 0;
    uint8_t intentosFallidos = 0;

    for (;;) 
    {
        if (xQueueReceive(xColaTeclas, &teclaRecibida, portMAX_DELAY) == pdPASS) 
        {
            
            // CASO 1: Confirmar Clave
            if (teclaRecibida == '#') 
            {
                if (indiceClave > 0) 
                {
                    bufferClave[indiceClave] = '\0'; // Cierre seguro de cadena

                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                    {
                        printf("\n[LCD] Verificando clave...\n");
                        xSemaphoreGive(xMutexLCD);
                    }

                    vTaskDelay(pdMS_TO_TICKS(500)); 

                    if (strcmp(bufferClave, CLAVE_CORRECTA) == 0) 
                    {
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            printf("[LCD] ACCESO PERMITIDO\n");
                            printf("[HARDWARE] Abriendo servo a 90 grados y encendiendo LED Verde\n");
                            xSemaphoreGive(xMutexLCD);
                        }
                        
                        intentosFallidos = 0;
                        vTaskDelay(pdMS_TO_TICKS(4000)); 

                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            printf("[HARDWARE] Cerrando servo a 0 grados y apagando LED Verde\n");
                            printf("[LCD] Ingrese Clave:\n");
                            xSemaphoreGive(xMutexLCD);
                        }
                    } 
                    else 
                    {
                        intentosFallidos++;
                        
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            printf("[LCD] CLAVE INCORRECTA. Intentos: %d/3\n", intentosFallidos);
                            xSemaphoreGive(xMutexLCD);
                        }
                        
                        vTaskDelay(pdMS_TO_TICKS(2000)); 

                        if (intentosFallidos >= 3) 
                        {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                printf("[LCD] SISTEMA BLOQUEADO. Espere 10s...\n");
                                printf("[HARDWARE] Encendiendo LED Rojo de Alarma\n");
                                xSemaphoreGive(xMutexLCD);
                            }

                            xQueueReset(xColaTeclas); 
                            vTaskDelay(pdMS_TO_TICKS(10000)); 
                            intentosFallidos = 0; 

                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                printf("[HARDWARE] Apagando LED Rojo de Alarma\n");
                                printf("[LCD] Ingrese Clave:\n");
                                xSemaphoreGive(xMutexLCD);
                            }
                        } 
                        else 
                        {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                printf("[LCD] Ingrese Clave:\n");
                                xSemaphoreGive(xMutexLCD);
                            }
                        }
                    }

                    memset(bufferClave, 0, sizeof(bufferClave));
                    indiceClave = 0;
                }
            } 
            // CASO 2: Cancelar/Borrar
            else if (teclaRecibida == '*') 
            {
                memset(bufferClave, 0, sizeof(bufferClave));
                indiceClave = 0;
                
                if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                {
                    printf("\n[LCD] Entrada borrada.\n");
                    printf("[LCD] Ingrese Clave:\n");
                    xSemaphoreGive(xMutexLCD);
                }
            } 
            // CASO 3: Acumular dígitos
            else 
            {
                if (indiceClave < 4) 
                {
                    bufferClave[indiceClave] = teclaRecibida;
                    indiceClave++;

                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                    {
                        printf("*"); 
                        fflush(stdout); 
                        xSemaphoreGive(xMutexLCD);
                    }
                }
            }
        }
    }
}