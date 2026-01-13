cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(bitdoglab_game C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- CONFIGURAÇÃO DO FREERTOS ---
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)

# Importa configuração do RP2040
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

add_executable(bitdoglab_game
    main.cpp
    # Adicionando fontes do FreeRTOS manualmente para garantir compilação
    ${FREERTOS_KERNEL_PATH}/event_groups.c
    ${FREERTOS_KERNEL_PATH}/timers.c
    ${FREERTOS_KERNEL_PATH}/queue.c
    ${FREERTOS_KERNEL_PATH}/tasks.c
    ${FREERTOS_KERNEL_PATH}/list.c
)

# Gera o header ws2812.pio.h
pico_generate_pio_header(bitdoglab_game ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

# Includes
target_include_directories(bitdoglab_game PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${FREERTOS_KERNEL_PATH}/include
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/include
)

# Bibliotecas
target_link_libraries(bitdoglab_game PRIVATE
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_interp
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(bitdoglab_game)


pico_enable_stdio_usb(bitdoglab_game 1)