#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_serial.h"

/* Configuracoes de Hardware para STM32F411CE BlackPill */
#define BUTTON_PIN      0U      // Botao USER: PA0 (ativo em nivel baixo / GND)
#define LED_PIN         13U     // LED Onboard: PC13 (ativo em nivel baixo)

/* Parametros de Temporizacao */
#define DEBOUNCE_MS     200U    // Janela de guarda para debounce mecânico (SysTick)

/* Variaveis compartilhadas entre a ISR (EXTI0) e o main loop.
 * Marcadas como 'volatile' para evitar otimizacoes indevidas do compilador. */
static volatile uint32_t last_press_time = 0U;
static volatile uint32_t press_count = 0U;
static volatile uint32_t press_timestamp = 0U;
static volatile uint8_t  button_event = 0U;

/**
 * @brief Inicializa o GPIO PC13 para controle do LED onboard.
 */
static void led_init(void)
{
    // 1) Habilita o clock do GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // 2) Configura PC13 como saida (MODER13 = 01)
    GPIOC->MODER &= ~(3U << (LED_PIN * 2U));
    GPIOC->MODER |=  (1U << (LED_PIN * 2U));

    // 3) Tipo Push-pull e sem resistores de pull-up/pull-down
    GPIOC->OTYPER &= ~(1U << LED_PIN);
    GPIOC->PUPDR  &= ~(3U << (LED_PIN * 2U));

    // LED onboard da BlackPill e ativo em nivel baixo.
    // Inicia com o LED apagado (PC13 = 1)
    GPIOC->BSRR = (1U << LED_PIN);
}

/**
 * @brief Inverte o estado do LED onboard (feedback visual).
 */
static inline void led_toggle(void)
{
    GPIOC->ODR ^= (1U << LED_PIN);
}

/**
 * @brief Inicializa a interrupcao externa (EXTI0) para o botao USER em PA0.
 */
static void button_interrupt_init(void)
{
    // 1) Habilita clock do GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // 2) Configura PA0 como entrada digital (MODER0 = 00)
    GPIOA->MODER &= ~(3U << (BUTTON_PIN * 2U));

    // 3) Habilita resistor de Pull-up interno em PA0
    //    Solto: PA0 = 1 (VCC)
    //    Pressionado: PA0 = 0 (GND)
    GPIOA->PUPDR &= ~(3U << (BUTTON_PIN * 2U));
    GPIOA->PUPDR |=  (1U << (BUTTON_PIN * 2U));

    // 4) Habilita o clock do periférico SYSCFG para roteamento EXTI
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // 5) Conecta a linha EXTI0 ao Port A (PA0)
    SYSCFG->EXTICR[0] &= ~(0xFU << 0U);

    // 6) Desenmascara a interrupcao na linha EXTI0
    EXTI->IMR |= EXTI_IMR_MR0;

    // 7) Disparo na borda de descida (quando o botao vai de 1 para 0)
    EXTI->RTSR &= ~EXTI_RTSR_TR0;
    EXTI->FTSR |=  EXTI_FTSR_TR0;

    // 8) Limpa qualquer pendencia previa no bit PR
    EXTI->PR = EXTI_PR_PR0;

    // 9) Configura prioridade e habilita interrupcao no NVIC
    NVIC_SetPriority(EXTI0_IRQn, 5U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/**
 * @brief Tratador da interrupcao externa EXTI0 (Botao USER - PA0).
 *        Implementa o filtro de debounce por janela de guarda usando SysTick (ms_ticks).
 */
void EXTI0_IRQHandler(void)
{
    // Verifica se a interrupcao provem da linha EXTI0
    if ((EXTI->PR & EXTI_PR_PR0) != 0U)
    {
        // Limpa a pendencia escrevendo 1 no bit correspondente
        EXTI->PR = EXTI_PR_PR0;

        // Amostragem da base de tempo do SysTick
        uint32_t current_time = ms_ticks;

        // Debounce temporal na ISR:
        // Se a diferenca de tempo for superior ao periodo de acomodacao mecanica,
        // valida o clique e rejeita reacionamentos/ruidos espurios.
        if ((current_time - last_press_time) >= DEBOUNCE_MS)
        {
            last_press_time = current_time;
            press_count++;
            press_timestamp = current_time;
            button_event = 1U;
        }
    }
}

int main(void)
{
    // Inicializacoes dos perifericos e subsistemas
    usb_serial_init();
    led_init();
    button_interrupt_init();

    printf("\r\n========================================\r\n");
    printf("   Atividade 3 - Tarefa A: Debounce EXTI\r\n");
    printf("========================================\r\n");
    fflush(stdout);

    while (1)
    {
        tud_task();
        // Verifica se houve evento de clique valido registrado pela ISR
        if (button_event != 0U)
        {
            // Secao critica breve para leitura segura e atomica das variaveis da ISR
            __disable_irq();
            uint32_t count = press_count;
            uint32_t timestamp = press_timestamp;
            button_event = 0U;
            __enable_irq();

            // Feedback visual: inverte o LED onboard
            led_toggle();

            // Envio via serial no formato estipulado:
            // "Botao X | pressionado em Y ms"
            printf("Botao %lu | pressionado em %lu ms\r\n", (unsigned long)count, (unsigned long)timestamp);
            fflush(stdout);
        }
    }
}