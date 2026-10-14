#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usb_serial.h"

/* Configuracoes de Hardware para STM32F411CE BlackPill */
#define BUTTON_PIN          0U      // Botao USER: PA0 (ativo em nivel baixo / GND)
#define LED_PIN             13U     // LED Onboard: PC13 (ativo em nivel baixo)

/* Parametros de Temporizacao e Classificacao Morse */
#define DEBOUNCE_MS         30U     // Janela de debounce por borda
#define DOT_MAX_MS          300U    // Duracao maxima em ms para ser classificado como Ponto (.)
#define LETTER_TIMEOUT_MS   1000U   // 1 segundo sem novo clique indica fim da letra
#define MAX_MORSE_LEN       8U      // Quantidade maxima de simbolos por letra

/* Estados da FSM (Maquina de Estados Finitos) */
typedef enum
{
    MORSE_STATE_IDLE,           // Aguardando primeiro toque
    MORSE_STATE_COLLECTING,     // Coletando pontos/tracos e monitorando timeout de 1s
    MORSE_STATE_DECODE          // Timeout de 1s atingido: decodifica e imprime letra
} morse_state_t;

/* Tabela de Decodificacao Morse (A-Z, 0-9) */
typedef struct
{
    const char *morse;
    char letter;
} morse_symbol_map_t;

static const morse_symbol_map_t morse_table[] = {
    { ".-",    'A' }, { "-...",  'B' }, { "-.-.",  'C' }, { "-..",   'D' },
    { ".",     'E' }, { "..-.",  'F' }, { "--.",   'G' }, { "....",  'H' },
    { "..",    'I' }, { ".---",  'J' }, { "-.-",   'K' }, { ".-..",  'L' },
    { "--",    'M' }, { "-.",    'N' }, { "---",   'O' }, { ".--.",  'P' },
    { "--.-",  'Q' }, { ".-.",   'R' }, { "...",   'S' }, { "-",     'T' },
    { "..-",   'U' }, { "...-",  'V' }, { ".--",   'W' }, { "-..-",  'X' },
    { "-.--",  'Y' }, { "--..",  'Z' },
    { "-----", '0' }, { ".----", '1' }, { "..---", '2' }, { "...--", '3' },
    { "....-", '4' }, { ".....", '5' }, { "-....", '6' }, { "--...", '7' },
    { "---..", '8' }, { "----.", '9' }
};

/* Variaveis compartilhadas entre a ISR e o main loop (marcadas como volatile) */
static volatile uint32_t last_edge_tick = 0U;
static volatile uint32_t press_start_tick = 0U;
static volatile uint32_t last_release_tick = 0U;
static volatile uint32_t pulse_duration_ms = 0U;
static volatile bool     btn_is_pressed = false;
static volatile uint8_t  pulse_ready = 0U;

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
 * @brief Configura interrupcao externa EXTI0 em ambas as bordas (descida e subida).
 *        Permite medir com precisao o tempo em que o botao permaneceu pressionado.
 */
static void button_interrupt_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER &= ~(3U << (BUTTON_PIN * 2U));

    // Pull-up interno: Solto = 1, Pressionado = 0
    GPIOA->PUPDR &= ~(3U << (BUTTON_PIN * 2U));
    GPIOA->PUPDR |=  (1U << (BUTTON_PIN * 2U));

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    SYSCFG->EXTICR[0] &= ~(0xFU << 0U); // Roteia PA0 para EXTI0
    EXTI->IMR |= EXTI_IMR_MR0;

    // Habilita disparo em AMBAS as bordas (descida ao apertar, subida ao soltar)
    EXTI->FTSR |= EXTI_FTSR_TR0;
    EXTI->RTSR |= EXTI_RTSR_TR0;

    EXTI->PR = EXTI_PR_PR0;

    NVIC_SetPriority(EXTI0_IRQn, 5U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/**
 * @brief Tratador da interrupcao EXTI0 (Botao USER - PA0).
 *        Detecta as bordas de descida e subida para cronometrar a duracao do pressionamento.
 */
void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U)
    {
        EXTI->PR = EXTI_PR_PR0; // Limpa flag de pendencia

        uint32_t now = ms_ticks;
        uint32_t pin_state = (GPIOA->IDR & (1U << BUTTON_PIN));

        if (pin_state == 0U)
        {
            // Borda de descida: Botao foi pressionado
            if (!btn_is_pressed)
            {
                if ((now - last_edge_tick) >= DEBOUNCE_MS)
                {
                    btn_is_pressed = true;
                    press_start_tick = now;
                    last_edge_tick = now;
                    led_on(); // Feedback visual instantaneo ao segurar o botao
                }
            }
        }
        else
        {
            // Borda de subida: Botao foi liberado
            if (btn_is_pressed)
            {
                if ((now - last_edge_tick) >= DEBOUNCE_MS)
                {
                    btn_is_pressed = false;
                    last_edge_tick = now;
                    last_release_tick = now;
                    pulse_duration_ms = now - press_start_tick;
                    pulse_ready = 1U;
                    led_off(); // Apaga LED ao soltar
                }
            }
        }
    }
}

/**
 * @brief Busca a letra correspondente a sequencia Morse na tabela ITU.
 */
static char decode_morse(const char *code)
{
    size_t total = sizeof(morse_table) / sizeof(morse_table[0]);
    for (size_t i = 0; i < total; ++i)
    {
        if (strcmp(morse_table[i].morse, code) == 0)
        {
            return morse_table[i].letter;
        }
    }
    return '?'; // Nao encontrado
}

int main(void)
{
    usb_serial_init();
    led_init();
    button_interrupt_init();

    morse_state_t state = MORSE_STATE_IDLE;
    char morse_buffer[MAX_MORSE_LEN + 1];
    uint8_t morse_len = 0;
    memset(morse_buffer, 0, sizeof(morse_buffer));

    printf("\r\n==============================================\r\n");
    printf("   Atividade 3 - Tarefa C: Decodificador Morse\r\n");
    printf("==============================================\r\n");
    printf("Toque curto (< 300 ms)  = Ponto (.)\r\n");
    printf("Toque longo (>= 300 ms) = Traco (-)\r\n");
    printf("Pausa de 1 segundo      = Fim de Letra / Decodificar\r\n\r\n");
    fflush(stdout);

    while (1)
    {
        usb_serial_task();
        switch (state)
        {
            case MORSE_STATE_IDLE:
            {
                // Aguarda o primeiro pulso ser finalizado
                if (pulse_ready != 0U)
                {
                    __disable_irq();
                    uint32_t duration = pulse_duration_ms;
                    pulse_ready = 0U;
                    __enable_irq();

                    // Classifica o primeiro simbolo: ponto ou traco
                    char symbol = (duration < DOT_MAX_MS) ? '.' : '-';
                    morse_buffer[0] = symbol;
                    morse_buffer[1] = '\0';
                    morse_len = 1;

                    printf("Simbolo: '%c' (%lu ms) | Sequencia: %s\r\n", symbol, (unsigned long)duration, morse_buffer);
                    fflush(stdout);

                    state = MORSE_STATE_COLLECTING;
                }
                break;
            }

            case MORSE_STATE_COLLECTING:
            {
                // Novo pulso registrado
                if (pulse_ready != 0U)
                {
                    __disable_irq();
                    uint32_t duration = pulse_duration_ms;
                    pulse_ready = 0U;
                    __enable_irq();

                    if (morse_len < MAX_MORSE_LEN)
                    {
                        char symbol = (duration < DOT_MAX_MS) ? '.' : '-';
                        morse_buffer[morse_len++] = symbol;
                        morse_buffer[morse_len] = '\0';

                        printf("Simbolo: '%c' (%lu ms) | Sequencia: %s\r\n", symbol, (unsigned long)duration, morse_buffer);
                        fflush(stdout);
                    }
                }

                // Verifica se passou 1 segundo sem novo acionamento (apenas com o botao solto)
                if (!btn_is_pressed && ((ms_ticks - last_release_tick) >= LETTER_TIMEOUT_MS))
                {
                    state = MORSE_STATE_DECODE;
                }
                break;
            }

            case MORSE_STATE_DECODE:
            {
                char decoded_char = decode_morse(morse_buffer);

                // Imprime a letra decodificada via serial conforme o enunciado
                printf(">> [Fim da letra] Morse: \"%s\" -> Letra: %c\r\n\r\n", morse_buffer, decoded_char);
                fflush(stdout);

                // Reseta o buffer para a proxima letra
                memset(morse_buffer, 0, sizeof(morse_buffer));
                morse_len = 0;

                state = MORSE_STATE_IDLE;
                break;
            }

            default:
            {
                state = MORSE_STATE_IDLE;
                break;
            }
        }
    }
}