#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_serial.h"

/* Configuracoes de Hardware para STM32F411CE BlackPill */
#define BUTTON_PIN      0U      // Botao USER: PA0 (ativo em nivel baixo / GND)
#define LED_PIN         13U     // LED Onboard: PC13 (ativo em nivel baixo)

/* Parametros de Temporizacao */
#define DEBOUNCE_MS         50U     // Janela de debounce para detecção rápida do clique
#define RANDOM_DELAY_MIN_MS 2000U   // Tempo minimo de espera secreta (2 segundos)
#define RANDOM_DELAY_MAX_MS 5000U   // Tempo maximo de espera secreta (5 segundos)
#define COOLDOWN_DELAY_MS   2500U   // Intervalo antes de reiniciar uma nova rodada

/* Estados da FSM (Maquina de Estados Finitos) */
typedef enum
{
    STATE_TEST_READY,           // Inicializa a rodada e envia aviso de pronto
    STATE_WAIT_RANDOM_DELAY,    // Espera secreta com verificacao de queima de largada
    STATE_WAIT_REACTION,        // LED aceso, medindo tempo ate o clique do usuario
    STATE_ROUND_COOLDOWN        // Pausa pos-resultado antes de iniciar novo teste
} reaction_state_t;

/* Variaveis compartilhadas com a ISR (marcadas como volatile) */
static volatile uint32_t last_btn_irq_tick = 0U;
static volatile uint32_t button_press_timestamp = 0U;
static volatile uint8_t  button_event = 0U;

/* Variavel para semente do gerador pseudo-aleatorio */
static uint32_t prng_state = 0xA5A5A5A5U;

/**
 * @brief Gera um numero pseudo-aleatorio dentro de uma faixa [min, max].
 *        Algoritmo LCG (Linear Congruential Generator).
 */
static uint32_t get_random_range(uint32_t min, uint32_t max)
{
    // Alimenta a semente com o SysTick corrente para aumentar o entropia
    prng_state ^= ms_ticks;
    prng_state = (prng_state * 1664525U) + 1013904223U;

    return min + (prng_state % (max - min + 1U));
}

/**
 * @brief Inicializa o GPIO PC13 para controle do LED onboard.
 */
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    GPIOC->MODER &= ~(3U << (LED_PIN * 2U));
    GPIOC->MODER |=  (1U << (LED_PIN * 2U));

    GPIOC->OTYPER &= ~(1U << LED_PIN);
    GPIOC->PUPDR  &= ~(3U << (LED_PIN * 2U));

    // LED onboard e ativo em nivel baixo. Inicia apagado (PC13 = 1).
    GPIOC->BSRR = (1U << LED_PIN);
}

static inline void led_on(void)
{
    GPIOC->BSRR = (1U << (LED_PIN + 16U)); // Nivel baixo acende
}

static inline void led_off(void)
{
    GPIOC->BSRR = (1U << LED_PIN);          // Nivel alto apaga
}

/**
 * @brief Inicializa a interrupcao externa (EXTI0) no pino PA0.
 */
static void button_interrupt_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER &= ~(3U << (BUTTON_PIN * 2U));

    // Pull-up interno: Solto = 1, Pressionado = 0
    GPIOA->PUPDR &= ~(3U << (BUTTON_PIN * 2U));
    GPIOA->PUPDR |=  (1U << (BUTTON_PIN * 2U));

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    SYSCFG->EXTICR[0] &= ~(0xFU << 0U); // Linha EXTI0 mapeada para GPIOA
    EXTI->IMR |= EXTI_IMR_MR0;

    // Disparo na borda de descida (quando pressiona)
    EXTI->RTSR &= ~EXTI_RTSR_TR0;
    EXTI->FTSR |=  EXTI_FTSR_TR0;

    EXTI->PR = EXTI_PR_PR0;

    NVIC_SetPriority(EXTI0_IRQn, 5U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/**
 * @brief Tratador da interrupcao EXTI0 (Botao USER - PA0).
 *        Grava o timestamp do momento exato do clique com filtro de debounce.
 */
void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U)
    {
        EXTI->PR = EXTI_PR_PR0;

        uint32_t now = ms_ticks;
        if ((now - last_btn_irq_tick) >= DEBOUNCE_MS)
        {
            last_btn_irq_tick = now;
            button_press_timestamp = now;
            button_event = 1U;
        }
    }
}

int main(void)
{
    usb_serial_init();
    led_init();
    button_interrupt_init();

    reaction_state_t state = STATE_TEST_READY;
    uint32_t state_start_tick = 0U;
    uint32_t random_delay = 0U;
    uint32_t reaction_start_tick = 0U;

    printf("\r\n========================================\r\n");
    printf("   Atividade 3 - Tarefa B: Tempo de Reacao\r\n");
    printf("========================================\r\n");
    fflush(stdout);

    while (1)
    {
        tud_task();
        switch (state)
        {
            case STATE_TEST_READY:
            {
                // Garante LED apagado
                led_off();

                // Descarta eventuais cliques anteriores
                __disable_irq();
                button_event = 0U;
                __enable_irq();

                // Mensagem de teste pronto conforme enunciado
                printf("\r\nTeste pronto! Aguarde o LED acender...\r\n");
                fflush(stdout);

                // Sorteia tempo secreto em segundos (convertido em ms)
                random_delay = get_random_range(RANDOM_DELAY_MIN_MS, RANDOM_DELAY_MAX_MS);
                state_start_tick = ms_ticks;

                state = STATE_WAIT_RANDOM_DELAY;
                break;
            }

            case STATE_WAIT_RANDOM_DELAY:
            {
                // Se o usuário pressionar o botão antes do tempo expirar: QUEIMA DE LARGADA!
                if (button_event != 0U)
                {
                    __disable_irq();
                    button_event = 0U;
                    __enable_irq();

                    // Aborta e imprime mensagem especificada
                    printf("Muito cedo!\r\n");
                    fflush(stdout);

                    state_start_tick = ms_ticks;
                    state = STATE_ROUND_COOLDOWN;
                    break;
                }

                // Verifica se o tempo aleatorio em segredo decorreu
                if ((ms_ticks - state_start_tick) >= random_delay)
                {
                    // Acende o LED onboard
                    led_on();

                    // Limpa qualquer evento residual
                    __disable_irq();
                    button_event = 0U;
                    __enable_irq();

                    // Timestamp exato do disparo visual
                    reaction_start_tick = ms_ticks;

                    // Mensagem de inicio de reacao
                    printf("AGORA! Pressione o botao!\r\n");
                    fflush(stdout);

                    state = STATE_WAIT_REACTION;
                }
                break;
            }

            case STATE_WAIT_REACTION:
            {
                // Aguarda o clique do usuario apos o LED acender
                if (button_event != 0U)
                {
                    __disable_irq();
                    uint32_t press_tick = button_press_timestamp;
                    button_event = 0U;
                    __enable_irq();

                    // Apaga o LED onboard
                    led_off();

                    // Calcula o tempo de reacao
                    uint32_t reaction_time = 0U;
                    if (press_tick >= reaction_start_tick)
                    {
                        reaction_time = press_tick - reaction_start_tick;
                    }

                    // Envia via serial no formato estipulado:
                    // "Tempo de reacao: X ms"
                    printf("Tempo de reacao: %lu ms\r\n", (unsigned long)reaction_time);
                    fflush(stdout);

                    state_start_tick = ms_ticks;
                    state = STATE_ROUND_COOLDOWN;
                }
                break;
            }

            case STATE_ROUND_COOLDOWN:
            {
                // Aguarda um pequeno periodo com SysTick antes de reiniciar o teste
                if ((ms_ticks - state_start_tick) >= COOLDOWN_DELAY_MS)
                {
                    state = STATE_TEST_READY;
                }
                break;
            }

            default:
            {
                state = STATE_TEST_READY;
                break;
            }
        }
    }
}