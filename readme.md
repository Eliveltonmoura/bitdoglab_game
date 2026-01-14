# 🐍 Jogo Snake RTOS 

Este projeto implementa o clássico jogo da "Cobra" em uma matriz de LEDs 5x5, utilizando o **Raspberry Pi Pico** e o sistema operacional de tempo real **FreeRTOS**. O projeto foi desenvolvido como parte da disciplina de PROJETO E IMPLEMENTAÇÃO DE SISTEMAS OPERACIONAIS para demonstrar o gerenciamento de tarefas e recursos.

## 📋 Requisitos do Projeto

O sistema cumpre rigorosamente os seguintes requisitos de arquitetura RTOS:

* **5x Tarefas (Tasks):** Gerenciamento independente de Input, Lógica, Display, Telemetria e Heartbeat.
* **1x Fila (Queue):** Comunicação entre a tarefa de entrada (botões) e a lógica do jogo.
* **1x Mutex (Semáforo):** Proteção do estado global (coordenadas da cobra e maçã) contra condições de corrida.
* **1x Temporizador (Software Timer):** Incremento progressivo da dificuldade (velocidade) a cada 10 segundos.

---

## 🛠️ Arquitetura do Sistema

O software foi dividido em módulos independentes que se comunicam através das primitivas do FreeRTOS:

| Tarefa | Prioridade | Função |
| --- | --- | --- |
| **vTaskInput** | 3 (Alta) | Monitora os botões A e B com debounce e envia comandos para a `Queue`. |
| **vTaskLogic** | 2 (Média) | Processa a movimentação, colisões e consome os dados da `Queue`. |
| **vTaskDisplay** | 1 (Baixa) | Atualiza a matriz de LEDs WS2812 via PIO com base nos dados protegidos pelo `Mutex`. |
| **vTaskTelemetry** | 1 (Baixa) | Reporta pontuação e status do jogo via Serial USB. |
| **vTaskHeartbeat** | 1 (Baixa) | Pisca o LED onboard para sinalizar que o sistema está operante. |

---

## 🕹️ Como Jogar

1. **Botão A (Pino 5):** Gira a cobra para a esquerda (sentido anti-horário).
2. **Botão B (Pino 6):** Gira a cobra para a direita (sentido horário).
3. **Objetivo:** Comer as maçãs (LED Vermelho) para crescer.
4. **Game Over:** Ocorre ao bater no próprio corpo. O jogo reinicia automaticamente após 2 segundos (ou ao pressionar qualquer botão).

---

## 🚀 Como Compilar e Rodar

### Pré-requisitos

* Raspberry Pi Pico SDK instalado.
* CMake e Compilador ARM (`arm-none-eabi-gcc`).

### Passo a Passo

1. Clone o repositório:
```bash
git clone https://github.com/Eliveltonmoura/bitdoglab_game.git
cd bitdogla_game

```


2. Crie a pasta de build e compile:
```bash
mkdir build && cd build
cmake ..
make -j4

```


3. Copie o arquivo `bitdoglab_game.uf2` para o seu Raspberry Pi Pico em modo BOOTSEL.
Ex: cp bitdoglab_game.uf2 /media/$USER/RPI-RP2/
---

## 📊 Monitoramento

Para visualizar a telemetria do jogo, utilize um monitor serial (Baud Rate: 115200):

```text
Status: PLAYING | Score: 3 | Speed: 255 ms
Timer disparou! Aumentando velocidade...

```

---

**Desenvolvido por:** Elivelton Moura, Ronyer Lopes, Pedro Wilson

**Plataforma:** BitDogLab / RP2040

---