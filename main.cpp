#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

// FreeRTOS includes
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

/* =========================================================================
 * CONFIGURAÇÕES
 * ========================================================================= */
#define LED_PIN         7     
#define LED_COUNT       25    
#define BUTTON_A_PIN    5     
#define BUTTON_B_PIN    6     
#define ONBOARD_LED     13    

// Cores (GRB)
#define COLOR_OFF       0x000000
#define COLOR_SNAKE     0x002000 
#define COLOR_APPLE     0x000020 
#define COLOR_DEATH     0x000005 

typedef struct {
    int x;
    int y;
} Point;

Point snakeBody[25];
int snakeLength = 1;
Point apple;
bool gameOver = false;
int currentDir = 1; 
int gameSpeedDelay = 300; 

QueueHandle_t xQueueInput;      
SemaphoreHandle_t xMutexState;  
TimerHandle_t xGameTimer;       

PIO pio = pio0;
uint sm = 0;

/* =========================================================================
 * AUXILIARES
 * ========================================================================= */
void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

int getIndex(int x, int y) {
    if (y % 2 == 0) return 24 - (y * 5 + x);
    else return 24 - (y * 5 + (4 - x));
}

void spawnApple() {
    bool valid = false;
    while (!valid) {
        apple.x = rand() % 5;
        apple.y = rand() % 5;
        valid = true;
        for (int i = 0; i < snakeLength; i++) {
            if (apple.x == snakeBody[i].x && apple.y == snakeBody[i].y) valid = false;
        }
    }
}

void resetGame() {
    snakeLength = 1;
    snakeBody[0].x = 2;
    snakeBody[0].y = 2;
    currentDir = 1;
    spawnApple();
    gameOver = false;
    gameSpeedDelay = 300;
}

/* =========================================================================
 * TIMER CALLBACK
 * ========================================================================= */
void vTimerCallback(TimerHandle_t xTimer) {
    if (gameSpeedDelay > 120) {
        gameSpeedDelay -= 15;
    }
}

/* =========================================================================
 * TAREFAS (REQUISITO: 5)
 * ========================================================================= */

// TAREFA 1: Input Melhorada (CORREÇÃO DOS BOTÕES)
void vTaskInput(void *pvParameters) {
    int cmd;
    while (true) {
        // Testa Botão A de forma independente
        if (!gpio_get(BUTTON_A_PIN)) {
            cmd = -1; 
            xQueueSend(xQueueInput, &cmd, 0);
           // printf("Botao A (Esquerda) OK\n"); // Debug
            vTaskDelay(pdMS_TO_TICKS(200)); 
        }
        
        // Testa Botão B de forma independente (sem ELSE)
        if (!gpio_get(BUTTON_B_PIN)) {
            cmd = 1; 
            xQueueSend(xQueueInput, &cmd, 0);
            //printf("Botao B (Direita) OK\n"); // Debug
            vTaskDelay(pdMS_TO_TICKS(200)); 
        }

        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

// TAREFA 2: Lógica (Com Auto-Reset e proteção de reinício)
void vTaskLogic(void *pvParameters) {
    int inputCmd;
    int gameOverTimer = 0; // Contador para reset automático
    
    resetGame();

    while (true) {
        // 1. Verifica comandos na fila
        if (xQueueReceive(xQueueInput, &inputCmd, 0) == pdTRUE) {
            if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
                if (gameOver) {
                    resetGame(); // Reinicia imediatamente se apertar botão no Game Over
                    gameOverTimer = 0;
                } else {
                    currentDir = (currentDir + inputCmd) % 4;
                    if (currentDir < 0) currentDir = 3;
                }
                xSemaphoreGive(xMutexState);
            }
        }

        // 2. Processa movimento ou espera de Reset
        if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
            if (!gameOver) {
                // Lógica normal de movimento
                for (int i = snakeLength; i > 0; i--) snakeBody[i] = snakeBody[i-1];

                if (currentDir == 0) snakeBody[0].y += 1;
                else if (currentDir == 1) snakeBody[0].x += 1;
                else if (currentDir == 2) snakeBody[0].y -= 1;
                else if (currentDir == 3) snakeBody[0].x -= 1;

                // Wrap-around
                if (snakeBody[0].x > 4) snakeBody[0].x = 0;
                else if (snakeBody[0].x < 0) snakeBody[0].x = 4;
                if (snakeBody[0].y > 4) snakeBody[0].y = 0;
                else if (snakeBody[0].y < 0) snakeBody[0].y = 4;

                // Colisão com corpo
                for (int i = 1; i < snakeLength; i++) {
                    if (snakeBody[0].x == snakeBody[i].x && snakeBody[0].y == snakeBody[i].y) {
                        gameOver = true;
                        gameOverTimer = 0; // Inicia contagem para reset
                    }
                }

                // Comer maçã
                if (snakeBody[0].x == apple.x && snakeBody[0].y == apple.y) {
                    snakeLength++;
                    if (snakeLength >= 25) resetGame();
                    else spawnApple();
                }
            } else {
                // Lógica de Reinício Automático (espera ~2 segundos)
                gameOverTimer += gameSpeedDelay;
                if (gameOverTimer >= 2000) { 
                    resetGame();
                    gameOverTimer = 0;
                }
            }
            xSemaphoreGive(xMutexState);
        }
        vTaskDelay(pdMS_TO_TICKS(gameSpeedDelay));
    }
}
// TAREFA 3: Display
void vTaskDisplay(void *pvParameters) {
    uint32_t buffer[LED_COUNT];
    while (true) {
        for (int i = 0; i < LED_COUNT; i++) buffer[i] = COLOR_OFF;
        if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
            if (gameOver) {
                for (int i = 0; i < LED_COUNT; i++) buffer[i] = COLOR_DEATH;
            } else {
                buffer[getIndex(apple.x, apple.y)] = 0x004000; // Maçã Vermelha (GRB)
                for (int i = 0; i < snakeLength; i++) {
                    buffer[getIndex(snakeBody[i].x, snakeBody[i].y)] = 0x200000; // Cobra Verde
                }
            }
            xSemaphoreGive(xMutexState);
        }
        for (int i = 0; i < LED_COUNT; i++) put_pixel(buffer[i]);
        vTaskDelay(pdMS_TO_TICKS(40)); 
    }
}

// TAREFA 4: Telemetria
void vTaskTelemetry(void *pvParameters) {
    while (true) {
        printf("Jogo rodando... Pontos: %d | Velocidade: %d ms\n", snakeLength, gameSpeedDelay);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// TAREFA 5: Heartbeat
void vTaskHeartbeat(void *pvParameters) {
    gpio_init(ONBOARD_LED);
    gpio_set_dir(ONBOARD_LED, GPIO_OUT);
    while (true) {
        gpio_put(ONBOARD_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(ONBOARD_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main() {
    stdio_init_all();
    gpio_init(BUTTON_A_PIN); gpio_set_dir(BUTTON_A_PIN, GPIO_IN); gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN); gpio_set_dir(BUTTON_B_PIN, GPIO_IN); gpio_pull_up(BUTTON_B_PIN);
    
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, false);

    xQueueInput = xQueueCreate(5, sizeof(int));
    xMutexState = xSemaphoreCreateMutex();
    xGameTimer = xTimerCreate("SnakeTimer", pdMS_TO_TICKS(10000), pdTRUE, 0, vTimerCallback);
    
    xTimerStart(xGameTimer, 0);
    xTaskCreate(vTaskInput, "In", 1024, NULL, 3, NULL);
    xTaskCreate(vTaskLogic, "Log", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskDisplay, "Disp", 1024, NULL, 1, NULL);
    xTaskCreate(vTaskTelemetry, "Tel", 1024, NULL, 1, NULL);
    xTaskCreate(vTaskHeartbeat, "HB", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1);
}

/* =========================================================================
 * HOOKS DO FREERTOS - Necessários para resolver erros de compilação
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

// Chamado se houver estouro de pilha (Stack Overflow) em alguma tarefa
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void) xTask;
    (void) pcTaskName;
    printf("ERRO: Stack Overflow na tarefa: %s\n", pcTaskName);
    while(1); // Trava o sistema para depuração
}

// Chamado se o malloc do FreeRTOS (pvPortMalloc) falhar por falta de memória heap
void vApplicationMallocFailedHook(void) {
    printf("ERRO: Falha de alocação de memória (Heap Full)\n");
    while(1); // Trava o sistema para depuração
}

#ifdef __cplusplus
}
#endif