#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "usb_serial.h"

/* Configuracoes de Hardware para STM32F411CE BlackPill */
#define BUTTON_PIN          0U      // Botao USER: PA0 (ativo em nivel baixo / GND)
#define LED_PIN             13U     // LED Onboard: PC13 (ativo em nivel baixo)

/* Parametros do Jogo Ritmico */
#define DEBOUNCE_MS         80U     // Janela de debounce do botao
#define INITIAL_SEQ_LEN     3U      // Tamanho inicial da sequencia: 3 trechos
#define MAX_SEQ_LEN         8U      // Tamanho maximo da sequencia

/* Duracoes nominais e faixas de tolerancia para classificacao */
#define SHORT_NOMINAL_MS    400U    // Duracao nominal do trecho curto
#define LONG_NOMINAL_MS     900U    // Duracao nominal do trecho longo
#define SHORT_MAX_MS        650U    // Limite superior para classificar como Curto
#define MIN_INTERVAL_MS     180U    // Limite minimo de intervalo humano
#define MAX_INTERVAL_MS     1600U   // Limite maximo de intervalo humano
#define USER_TIMEOUT_MS     3500U   // Timeout de inatividade do usuario
#define COOLDOWN_DELAY_MS   2500U   // Pausa entre rodadas

/* Tipos de Intervalo */
typedef enum
{
    RHYTHM_SHORT = 0,
    RHYTHM_LONG  = 1
} rhythm_type_t;

/* Estados da FSM (Maquina de Estados Finitos) */
typedef enum
{
    RHYTHM_STATE_DEMO_INIT,         // Apresenta a rodada e prepara demonstracao
    RHYTHM_STATE_DEMO_PLAY,         // Pisca o LED no ritmo alvo para demonstracao
    RHYTHM_STATE_USER_WAIT_START,   // Aguarda o primeiro clique do usuario
    RHYTHM_STATE_USER_RECORDING,    // Coleta e mede os intervalos entre os cliques
    RHYTHM_STATE_VALIDATE,          // Classifica a tentativa e imprime resultados
    RHYTHM_STATE_COOLDOWN           // Pausa antes da proxima rodada
} rhythm_state_t;

/* Variaveis compartilhadas com a ISR (marcadas como volatile) */
static volatile uint32_t last_btn_irq_tick = 0U;
static volatile uint32_t button_press_timestamp = 0U;
static volatile uint8_t  button_click_event = 0U;

/* Semente PRNG simples para gerar novos trechos */
static uint32_t prng_state = 0x12345678U;

static uint32_t get_random_bit(void)
{
    prng_state ^= ms_ticks;
    prng_state = (prng_state * 1664525U) + 1013904223U;
    return (prng_state >> 16U) & 1U;
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

    // Inicia apagado (PC13 = 1)
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

    SYSCFG->EXTICR[0] &= ~(0xFU << 0U); // Linha EXTI0 conectada a PA0
    EXTI->IMR |= EXTI_IMR_MR0;

    // Disparo na borda de descida (momento do clique)
    EXTI->RTSR &= ~EXTI_RTSR_TR0;
    EXTI->FTSR |=  EXTI_FTSR_TR0;

    EXTI->PR = EXTI_PR_PR0;

    NVIC_SetPriority(EXTI0_IRQn, 5U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/**
 * @brief Tratador da interrupcao EXTI0 (Botao USER - PA0).
 */
void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U)
    {
        EXTI->PR = EXTI_PR_PR0; // Limpa flag de pendencia

        uint32_t now = ms_ticks;
        if ((now - last_btn_irq_tick) >= DEBOUNCE_MS)
        {
            last_btn_irq_tick = now;
            button_press_timestamp = now;
            button_click_event = 1U;
        }
    }
}

static const char *get_type_name(rhythm_type_t type)
{
    return (type == RHYTHM_SHORT) ? "CURTO" : "LONGO";
}

int main(void)
{
    usb_serial_init();
    led_init();
    button_interrupt_init();

    rhythm_state_t state = RHYTHM_STATE_DEMO_INIT;

    // Sequencia alvo de trechos (intervalos)
    rhythm_type_t target_sequence[MAX_SEQ_LEN] = {
        RHYTHM_SHORT, RHYTHM_LONG, RHYTHM_SHORT
    };
    uint8_t current_seq_len = INITIAL_SEQ_LEN;

    // Variaveis de medicao do usuario
    uint32_t user_intervals[MAX_SEQ_LEN];
    uint8_t  intervals_collected = 0;
    uint32_t last_user_click_tick = 0U;

    // Variaveis para a demonstracao visual no LED
    uint8_t  demo_step = 0;
    uint32_t demo_timer = 0U;
    bool     demo_led_state = false;

    uint32_t cooldown_start_tick = 0U;

    printf("\r\n===================================================\r\n");
    printf("   Atividade 3 - Tarefa D: Jogo de Sequencia Ritmica\r\n");
    printf("===================================================\r\n");
    printf("Curto = ~%lu ms | Longo = ~%lu ms\r\n", (unsigned long)SHORT_NOMINAL_MS, (unsigned long)LONG_NOMINAL_MS);
    fflush(stdout);

    while (1)
    {
        switch (state)
        {
            case RHYTHM_STATE_DEMO_INIT:
            {
                printf("\r\n--- Nivel com %u Trechos ---\r\n", (unsigned int)current_seq_len);
                printf("Padrao alvo: [");
                for (uint8_t i = 0; i < current_seq_len; ++i)
                {
                    printf("%s%s", get_type_name(target_sequence[i]), (i + 1 < current_seq_len) ? " - " : "");
                }
                printf("]\r\nDemonstrando ritmo no LED onboard...\r\n");
                fflush(stdout);

                // Prepara demonstracao no LED
                demo_step = 0;
                demo_led_state = true;
                led_on();
                demo_timer = ms_ticks;

                state = RHYTHM_STATE_DEMO_PLAY;
                break;
            }

            case RHYTHM_STATE_DEMO_PLAY:
            {
                // Pisca o LED indicando o pulso e aguarda o intervalo entre pulsos
                if (demo_led_state)
                {
                    // Pulso aceso dura 100 ms
                    if ((ms_ticks - demo_timer) >= 100U)
                    {
                        led_off();
                        demo_led_state = false;
                        demo_timer = ms_ticks;
                    }
                }
                else
                {
                    // LED apagado durante o intervalo do trecho corrente
                    if (demo_step < current_seq_len)
                    {
                        uint32_t interval = (target_sequence[demo_step] == RHYTHM_SHORT) ? SHORT_NOMINAL_MS : LONG_NOMINAL_MS;
                        if ((ms_ticks - demo_timer) >= interval)
                        {
                            demo_step++;
                            led_on();
                            demo_led_state = true;
                            demo_timer = ms_ticks;
                        }
                    }
                    else
                    {
                        // Demonstracao concluida
                        state = RHYTHM_STATE_USER_WAIT_START;
                    }
                }
                break;
            }

            case RHYTHM_STATE_USER_WAIT_START:
            {
                // Limpa eventos pendentes
                __disable_irq();
                button_click_event = 0U;
                __enable_irq();

                intervals_collected = 0;

                printf("\r\n>> SUA VEZ! Pressione o botao %u vezes no ritmo demonstrado!\r\n", (unsigned int)(current_seq_len + 1U));
                fflush(stdout);

                state = RHYTHM_STATE_USER_RECORDING;
                last_user_click_tick = ms_ticks; // Inicia monitoramento de timeout
                break;
            }

            case RHYTHM_STATE_USER_RECORDING:
            {
                // Verifica novo clique do botao
                if (button_click_event != 0U)
                {
                    __disable_irq();
                    uint32_t click_time = button_press_timestamp;
                    button_click_event = 0U;
                    __enable_irq();

                    // Feedback visual breve
                    led_on();

                    if (intervals_collected == 0 && last_user_click_tick == 0U)
                    {
                        // Primeiro clique da serie (marco zero)
                        last_user_click_tick = click_time;
                        printf("Clique 1 registrado! Continue o ritmo...\r\n");
                        fflush(stdout);
                    }
                    else if (last_user_click_tick != 0U)
                    {
                        // Mede o intervalo exato entre cliques usando SysTick
                        uint32_t delta = click_time - last_user_click_tick;
                        user_intervals[intervals_collected] = delta;
                        intervals_collected++;
                        last_user_click_tick = click_time;

                        printf("Clique %u | Intervalo %u: %lu ms\r\n", 
                               (unsigned int)(intervals_collected + 1U),
                               (unsigned int)intervals_collected,
                               (unsigned long)delta);
                        fflush(stdout);

                        // Se completou todos os trechos da sequencia
                        if (intervals_collected >= current_seq_len)
                        {
                            led_off();
                            state = RHYTHM_STATE_VALIDATE;
                            break;
                        }
                    }
                    led_off();
                }

                // Timeout de inatividade do usuario (caso pare no meio)
                if (last_user_click_tick != 0U && ((ms_ticks - last_user_click_tick) >= USER_TIMEOUT_MS))
                {
                    printf(">> Tempo esgotado! Tentativa incompleta.\r\n");
                    fflush(stdout);
                    cooldown_start_tick = ms_ticks;
                    state = RHYTHM_STATE_COOLDOWN;
                }
                break;
            }

            case RHYTHM_STATE_VALIDATE:
            {
                printf("\r\n--- Validacao dos Intervalos ---\r\n");
                bool success = true;

                for (uint8_t i = 0; i < current_seq_len; ++i)
                {
                    uint32_t interval = user_intervals[i];
                    rhythm_type_t classified_type;

                    // Classifica o intervalo medido
                    if (interval < SHORT_MAX_MS)
                    {
                        classified_type = RHYTHM_SHORT;
                    }
                    else
                    {
                        classified_type = RHYTHM_LONG;
                    }

                    bool segment_ok = (classified_type == target_sequence[i]) &&
                                      (interval >= MIN_INTERVAL_MS) &&
                                      (interval <= MAX_INTERVAL_MS);

                    printf("Trecho %u: %4lu ms -> Classificado: %-5s | Alvo: %-5s [%s]\r\n",
                           (unsigned int)(i + 1U),
                           (unsigned long)interval,
                           get_type_name(classified_type),
                           get_type_name(target_sequence[i]),
                           segment_ok ? "OK" : "ERRO");

                    if (!segment_ok)
                    {
                        success = false;
                    }
                }

                if (success)
                {
                    printf("\r\n>> PARABENS! Voce acertou a sequencia!\r\n");
                    if (current_seq_len < MAX_SEQ_LEN)
                    {
                        // Aumenta a dificuldade adicionando um novo trecho
                        rhythm_type_t new_segment = (get_random_bit() == 0U) ? RHYTHM_SHORT : RHYTHM_LONG;
                        target_sequence[current_seq_len] = new_segment;
                        current_seq_len++;
                        printf("Dificuldade aumentada: Proxima rodada tera %u trechos!\r\n", (unsigned int)current_seq_len);
                    }
                    else
                    {
                        printf("Dificuldade maxima atingida (%u trechos)!\r\n", (unsigned int)MAX_SEQ_LEN);
                    }
                }
                else
                {
                    printf("\r\n>> ERROU! Ritmo fora da tolerancia. Tente novamente o mesmo nivel.\r\n");
                }
                fflush(stdout);

                cooldown_start_tick = ms_ticks;
                state = RHYTHM_STATE_COOLDOWN;
                break;
            }

            case RHYTHM_STATE_COOLDOWN:
            {
                if ((ms_ticks - cooldown_start_tick) >= COOLDOWN_DELAY_MS)
                {
                    state = RHYTHM_STATE_DEMO_INIT;
                }
                break;
            }

            default:
            {
                state = RHYTHM_STATE_DEMO_INIT;
                break;
            }
        }
    }
}