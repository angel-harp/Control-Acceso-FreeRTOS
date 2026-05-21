#include <stdio.h>
// Cabeceras estándar de FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Definición de constantes
#define PIN_CERRADURA 12
#define PIN_ALARMA    13
#define CLAVE_CORRECTA "1234"

// Manejadores de los recursos de FreeRTOS
QueueHandle_t xColaTeclas = NULL;
SemaphoreHandle_t xMutexLCD = NULL;

// Prototipos de las tareas concurrentes
void vTareaEscanearTeclado(void *pvParameters);
void vTareaProcesarSeguridad(void *pvParameters);

// Función principal de C (Punto de entrada)
int main(void) 
{
    // 1. Inicialización del hardware ficticio/real (Pantalla, pines, etc.)
    printf("Iniciando Sistema de Control de Acceso...\n");

    // 2. Creación de los mecanismos de sincronización de RTOS
    xColaTeclas = xQueueCreate(10, sizeof(char));
    xMutexLCD = xSemaphoreCreateMutex();

    if (xColaTeclas != NULL && xMutexLCD != NULL) 
    {
        // 3. Creación de tareas con prioridades asignadas
        xTaskCreate(vTareaEscanearTeclado, "Teclado", 2048, NULL, 2, NULL);
        xTaskCreate(vTareaProcesarSeguridad, "Seguridad", 2048, NULL, 3, NULL);

        // 4. Arrancar el planificador (Scheduler) de FreeRTOS
        vTaskStartScheduler();
    }

    // Si el planificador inicia correctamente, el programa nunca llegará aquí
    for (;;);
    return 0;
}

/* ------------------- DESARROLLO DE TAREAS ------------------- */

void vTareaEscanearTeclado(void *pvParameters) 
{
    char teclaSimulada = '1'; // Aquí iría la lectura real de tu hardware
    for (;;) 
    {
        // Lógica para detectar pulsación física...
        
        // Enviamos el dato a la cola de forma segura
        xQueueSend(xColaTeclas, &teclaSimulada, 0);

        // Bloqueo periódico de la tarea por 40 milisegundos
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void vTareaProcesarSeguridad(void *pvParameters) 
{
    char teclaRecibida;
    char bufferClave[5] = {0}; // Almacena los 4 dígitos + el terminador nulo '\0'
    uint8_t indiceClave = 0;
    uint8_t intentosFallidos = 0;

    for (;;) 
    {
        // El portMAX_DELAY hace que la tarea no consuma NADA de CPU hasta que llegue una tecla
        if (xQueueReceive(xColaTeclas, &teclaRecibida, portMAX_DELAY) == pdPASS) 
        {
            
            // CASO 1: Confirmar Clave con la tecla '#'
            if (teclaRecibida == '#') 
            {
                
                // Aseguramos que solo procese si el usuario ingresó algo
                if (indiceClave > 0) 
                {
                    bufferClave[indiceClave] = '\0'; // Cerramos la cadena de texto de forma segura

                    // Exclusión mutua para escribir en la pantalla sin interrupciones
                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                    {
                        printf("\n[LCD] Verificando clave...\n");
                        xSemaphoreGive(xMutexLCD);
                    }

                    // Simulamos un pequeño tiempo de procesamiento del sistema
                    vTaskDelay(pdMS_TO_TICKS(500)); 

                    // Verificación de la contraseña
                    if (strcmp(bufferClave, CLAVE_CORRECTA) == 0) 
                    {
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            printf("[LCD] ACCESO PERMITIDO\n");
                            printf("[HARDWARE] Abriendo servo a 90 grados y encendiendo LED Verde\n");
                            xSemaphoreGive(xMutexLCD);
                        }
                        
                        intentosFallidos = 0; // Reseteamos el contador de errores

                        // Mantenemos la puerta abierta por 4 segundos eficientemente
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
                        // Clave incorrecta
                        intentosFallidos++;
                        
                        if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                        {
                            printf("[LCD] CLAVE INCORRECTA. Intentos: %d/3\n", intentosFallidos);
                            xSemaphoreGive(xMutexLCD);
                        }
                        
                        vTaskDelay(pdMS_TO_TICKS(2000)); // Mostrar el error por 2 segundos

                        // CONTROL DE BLOQUEO POR SEGURIDAD
                        if (intentosFallidos >= 3) 
                        {
                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            {
                                printf("[LCD] SISTEMA BLOQUEADO. Espere 10s...\n");
                                printf("[HARDWARE] Encendiendo LED Rojo de Alarma\n");
                                xSemaphoreGive(xMutexLCD);
                            }

                            // ¡TRUCO DE RTOS!: Vaciamos la cola para ignorar cualquier tecla 
                            // que el usuario presione desesperadamente durante el bloqueo.
                            xQueueReset(xColaTeclas); 

                            // La penalización congela ESTA tarea por 10 segundos libres de CPU
                            vTaskDelay(pdMS_TO_TICKS(10000)); 

                            intentosFallidos = 0; // Reiniciamos contador tras el castigo

                            if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) 
                            
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

                    // Limpiamos el buffer local para el próximo intento
                    memset(bufferClave, 0, sizeof(bufferClave));
                    indiceClave = 0;
                }
            } 
            // CASO 2: Cancelar/Borrar con la tecla '*'
            else if (teclaRecibida == '*') {
                memset(bufferClave, 0, sizeof(bufferClave));
                indiceClave = 0;
                
                if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                    printf("\n[LCD] Entrada borrada.\n");
                    printf("[LCD] Ingrese Clave:\n");
                    xSemaphoreGive(xMutexLCD);
                }
            } 
            // CASO 3: Acumular dígitos numéricos
            else {
                // Solo permitimos ingresar un máximo de 4 dígitos
                if (indiceClave < 4) {
                    bufferClave[indiceClave] = teclaRecibida;
                    indiceClave++;

                    // Máscara de privacidad (escribimos un asterisco por cada dígito)
                    if (xSemaphoreTake(xMutexLCD, portMAX_DELAY) == pdTRUE) {
                        printf("*"); 
                        fflush(stdout); // Fuerza a que se imprima de inmediato en consola
                        xSemaphoreGive(xMutexLCD);
                    }
                }
            }
        }
    }
}