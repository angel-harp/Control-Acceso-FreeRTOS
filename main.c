#include <stdio.h>
// Cabeceras nativas de FreeRTOS
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

int main(void) {
    printf("Inicializando Sistema de Seguridad RTOS...\n");

    // Crear herramientas de sincronización
    xColaTeclas = xQueueCreate(10, sizeof(char));
    xMutexLCD = xSemaphoreCreateMutex();

    if (xColaTeclas != NULL && xMutexLCD != NULL) {
        // Crear tareas concurrentes
        xTaskCreate(vTareaEscanearTeclado, "Teclado", 2048, NULL, 2, NULL);
        xTaskCreate(vTareaProcesarSeguridad, "Seguridad", 2048, NULL, 3, NULL);

        // Iniciar el planificador de FreeRTOS
        vTaskStartScheduler();
    }

    // Si el Scheduler inicia, el programa entra en las tareas y nunca llega aquí
    for (;;);
    return 0;
}

/* --- Implementación de Tareas --- */

void vTareaEscanearTeclado(void *pvParameters) {
    char teclaDetectada;
    for (;;) {
        // [Aquí irá el código para leer tu teclado físico]

        // Ejemplo de envío a la cola (cuando se detecte una tecla real)
        // xQueueSend(xColaTeclas, &teclaDetectada, 0);

        vTaskDelay(pdMS_TO_TICKS(40)); // Muestreo cada 40ms
    }
}

void vTareaProcesarSeguridad(void *pvParameters) {
    char teclaRecibida;
    for (;;) {
        // Bloqueo eficiente: Espera indefinidamente a que llegue una tecla
        if (xQueueReceive(xColaTeclas, &teclaRecibida, portMAX_DELAY) == pdPASS) {
            
            // Proteger la escritura de la pantalla con el Mutex
            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                // [Aquí irá tu lógica para acumular los 4 dígitos y mover el servo]
                
                xSemaphoreGive(xMutexLCD); // Liberar recurso
            }
        }
    }
}