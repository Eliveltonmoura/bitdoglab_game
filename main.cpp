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
#define LED_PIN         7     // Pino da Matriz de LEDs
#define LED_COUNT       25    
#define BUTTON_A_PIN    5   // Botão A
#define BUTTON_B_PIN    6   // Botão B
#define ONBOARD_LED     13    // LED vermelho da BitDogLab (ou 25 no Pico padrão)

// Cores
#define COLOR_OFF       0x000000
#define COLOR_SNAKE     0x002000 // Verde
#define COLOR_APPLE     0x000020 // Vermelho (ajustado para GRB)
#define COLOR_DEATH     0x000005 // Azul

/* =========================================================================
 * ESTRUTURAS E GLOBAIS
 * ========================================================================= */
typedef struct {
    int x;
    int y;
} Point;

// Variáveis do Jogo (Recurso Partilhado)
Point snakeBody[25];
int snakeLength = 1;
Point apple;
bool gameOver = false;
int currentDir = 1; // 0=Cima, 1=Direita, 2=Baixo, 3=Esquerda
int gameSpeedDelay = 300; // Velocidade inicial

// Handles do RTOS
QueueHandle_t xQueueInput;      // [REQUISITO: 1x Queue]
SemaphoreHandle_t xMutexState;  // [REQUISITO: 1x Mutex]
TimerHandle_t xGameTimer;       // [REQUISITO: 1x Timer]

// PIO Global
PIO pio = pio0;
uint sm = 0;

/* =========================================================================
 * FUNÇÕES AUXILIARES
 * ========================================================================= */

void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

int getIndex(int x, int y) {
    // Conversão Zig-Zag
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
 * [REQUISITO: 1x Temporizador]
 * Callback do Timer: Aumenta a dificuldade a cada 10 segundos
 * ========================================================================= */
void vTimerCallback(TimerHandle_t xTimer) {
    // Apenas sinaliza ou altera uma variável atómica
    if (gameSpeedDelay > 100) {
        gameSpeedDelay -= 20; // Acelera o jogo
        printf("Timer disparou! Aumentando velocidade para %dms\n", gameSpeedDelay);
    }
}

/* =========================================================================
 * [REQUISITO: 5x Tarefas]
 * ========================================================================= */

// TAREFA 1: Input (Lê botões -> Envia para Fila)
void vTaskInput(void *pvParameters) {
    int cmd;
    while (true) {
        cmd = -1;
        // Botão A: Anti-horário (-1)
        if (!gpio_get(BUTTON_A_PIN)) cmd = -1;
        // Botão B: Horário (+1)
        else if (!gpio_get(BUTTON_B_PIN)) cmd = 1;

        if (cmd != -1) {
            // [USO DA QUEUE] Envia o comando para a lógica
            xQueueSend(xQueueInput, &cmd, 0);
            vTaskDelay(pdMS_TO_TICKS(200)); // Debounce
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// TAREFA 2: Lógica do Jogo (Recebe da Fila -> Atualiza Estado Protegido)
void vTaskLogic(void *pvParameters) {
    int inputCmd;
    resetGame();

    while (true) {
        // 1. Processa Input da Fila (não bloqueante ou pouco tempo)
        if (xQueueReceive(xQueueInput, &inputCmd, 0) == pdTRUE) {
            // [USO DO MUTEX] Protege alteração de direção crítica
            if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
                currentDir += inputCmd;
                if (currentDir < 0) currentDir = 3;
                if (currentDir > 3) currentDir = 0;
                xSemaphoreGive(xMutexState);
            }
        }

        // 2. Atualiza Física
        // [USO DO MUTEX] Início da Secção Crítica
        if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
            if (!gameOver) {
                // Move corpo
                for (int i = snakeLength; i > 0; i--) {
                    snakeBody[i] = snakeBody[i-1];
                }
                // Move cabeça
                if (currentDir == 0) snakeBody[0].y += 1;
                if (currentDir == 1) snakeBody[0].x += 1;
                if (currentDir == 2) snakeBody[0].y -= 1;
                if (currentDir == 3) snakeBody[0].x -= 1;

                // Wrap (Teletransporte)
                if (snakeBody[0].x > 4) snakeBody[0].x = 0;
                if (snakeBody[0].x < 0) snakeBody[0].x = 4;
                if (snakeBody[0].y > 4) snakeBody[0].y = 0;
                if (snakeBody[0].y < 0) snakeBody[0].y = 4;

                // Colisão corpo
                for (int i = 1; i < snakeLength; i++) {
                    if (snakeBody[0].x == snakeBody[i].x && snakeBody[0].y == snakeBody[i].y) {
                        gameOver = true;
                    }
                }

                // Maçã
                if (snakeBody[0].x == apple.x && snakeBody[0].y == apple.y) {
                    snakeLength++;
                    if (snakeLength > 24) resetGame();
                    else spawnApple();
                }
            } else {
                // Reiniciar se receber input na fila
                if (inputCmd != 0 && inputCmd != -2) { // Qualquer valor válido
                    resetGame();
                }
            }
            xSemaphoreGive(xMutexState); // [FIM DO MUTEX]
        }

        vTaskDelay(pdMS_TO_TICKS(gameSpeedDelay));
    }
}

// TAREFA 3: Display (Lê Estado Protegido -> Atualiza LEDs)
void vTaskDisplay(void *pvParameters) {
    uint32_t buffer[LED_COUNT];
    
    while (true) {
        // Limpa buffer local
        for (int i = 0; i < LED_COUNT; i++) buffer[i] = COLOR_OFF;

        // [USO DO MUTEX] Lê o estado do jogo
        if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
            if (gameOver) {
                for (int i = 0; i < LED_COUNT; i++) buffer[i] = COLOR_DEATH;
            } else {
                // Maçã
                int appleIdx = getIndex(apple.x, apple.y);
                buffer[appleIdx] = 0x005000; // G=0, R=50, B=0
                // Cobra
                for (int i = 0; i < snakeLength; i++) {
                    int idx = getIndex(snakeBody[i].x, snakeBody[i].y);
                    buffer[idx] = 0x200000; // G=20, R=0, B=0
                }
            }
            xSemaphoreGive(xMutexState);
        }

        // Envia para o Hardware (Fora do Mutex para não bloquear lógica)
        for (int i = 0; i < LED_COUNT; i++) {
            put_pixel(buffer[i]);
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // ~20 FPS
    }
}

// TAREFA 4: Telemetria (Lê Estado Protegido -> Printf Serial)
void vTaskTelemetry(void *pvParameters) {
    while (true) {
        int len = 0;
        bool over = false;

        // [USO DO MUTEX] Leitura rápida
        if (xSemaphoreTake(xMutexState, portMAX_DELAY) == pdTRUE) {
            len = snakeLength;
            over = gameOver;
            xSemaphoreGive(xMutexState);
        }

        printf("Status: %s | Score: %d | Speed: %d ms\n", 
               over ? "GAME OVER" : "PLAYING", len, gameSpeedDelay);
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 segundo
    }
}

// TAREFA 5: Heartbeat (Pisca LED onboard)
void vTaskHeartbeat(void *pvParameters) {
    gpio_init(ONBOARD_LED);
    gpio_set_dir(ONBOARD_LED, GPIO_OUT);
    while (true) {
        gpio_put(ONBOARD_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(ONBOARD_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

/* =========================================================================
 * HOOKS
 * ========================================================================= */
extern "C" void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName ) { while(1); }
extern "C" void vApplicationMallocFailedHook( void ) { while(1); }

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main() {
    stdio_init_all();

    // Inicializa Hardware
    gpio_init(BUTTON_A_PIN); gpio_set_dir(BUTTON_A_PIN, GPIO_IN); gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN); gpio_set_dir(BUTTON_B_PIN, GPIO_IN); gpio_pull_up(BUTTON_B_PIN);
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, false);

    // 1. Cria Fila
    xQueueInput = xQueueCreate(10, sizeof(int));

    // 2. Cria Mutex
    xMutexState = xSemaphoreCreateMutex();

    // 3. Cria Timer (Auto-reload = true)
    xGameTimer = xTimerCreate("DifficultyTimer", pdMS_TO_TICKS(10000), pdTRUE, (void*)0, vTimerCallback);
    xTimerStart(xGameTimer, 0);

    // 4. Cria as 5 Tarefas
    xTaskCreate(vTaskInput, "Input", 1024, NULL, 2, NULL);      // Prioridade Alta (Input)
    xTaskCreate(vTaskLogic, "Logic", 1024, NULL, 2, NULL);      // Prioridade Alta (Lógica)
    xTaskCreate(vTaskDisplay, "Display", 1024, NULL, 1, NULL);  // Prioridade Média
    xTaskCreate(vTaskTelemetry, "Serial", 1024, NULL, 1, NULL); // Prioridade Baixa
    xTaskCreate(vTaskHeartbeat, "Led", 256, NULL, 1, NULL);     // Prioridade Baixa

    vTaskStartScheduler();

    while (1);
}