#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdio.h>
#include "usb_serial.h"

#define BUTTON_PIN  0U      // Botao USER da BlackPill: PA0
#define LED_PIN     13U     // LED onboard: PC13 (ativo em nivel baixo)

#define DEBOUNCE_MS 20U

extern volatile uint32_t ms_ticks;
// Alterada dentro da interrupcao e lida no programa principal.
// volatile impede o compilador de assumir que o valor nao muda sozinho.
volatile uint8_t button_event = 0U;

static void led_init(void)
{
    // 1) Habilita o clock do GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // 2) PC13 como saida (MODER13 = 01)
    GPIOC->MODER &= ~(3U << (LED_PIN * 2U));
    GPIOC->MODER |=  (1U << (LED_PIN * 2U));

    // Push-pull e sem pull-up/pull-down
    GPIOC->OTYPER &= ~(1U << LED_PIN);
    GPIOC->PUPDR  &= ~(3U << (LED_PIN * 2U));

    // LED da BlackPill e ativo em nivel baixo.
    // PC13 = 1 -> LED apagado.
    GPIOC->BSRR = (1U << LED_PIN);
}

static void button_interrupt_init(void)
{
    // 1) Habilita o clock do GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // 2) PA0 como entrada (MODER0 = 00)
    GPIOA->MODER &= ~(3U << (BUTTON_PIN * 2U));

    // 3) Pull-up interno em PA0.
    // Solto: PA0 = 1
    // Pressionado: PA0 = 0
    GPIOA->PUPDR &= ~(3U << (BUTTON_PIN * 2U));
    GPIOA->PUPDR |=  (1U << (BUTTON_PIN * 2U));

    // 4) Habilita o bloco SYSCFG, que conecta GPIO -> EXTI
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // 5) Seleciona PA0 como fonte da linha EXTI0.
    // EXTICR1[3:0] = 0000 -> Port A
    SYSCFG->EXTICR[0] &= ~(0xFU << 0U);

    // 6) Libera a linha EXTI0 para gerar interrupcao
    EXTI->IMR |= EXTI_IMR_MR0;

    // 7) Queremos interrupcao quando o botao for pressionado.
    // Como PA0 vai de 1 para 0, usamos borda de descida.
    EXTI->RTSR &= ~EXTI_RTSR_TR0;
    EXTI->FTSR |=  EXTI_FTSR_TR0;

    // Limpa eventual interrupcao pendente antes de habilitar o NVIC
    EXTI->PR = EXTI_PR_PR0;

    // 8) Configura o NVIC do Cortex-M4
    NVIC_SetPriority(EXTI0_IRQn, 5U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U)
    {
        // No STM32F4, escreve-se 1 no bit PR para limpar a pendencia.
        EXTI->PR = EXTI_PR_PR0;

        // A ISR apenas registra o evento.
        // O trabalho principal fica fora da interrupcao.
        button_event = 1U;
    }
}

void delay_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;

    while ((ms_ticks - start) < ms)
    {
        // espera
    }
}

// Coloca pino em nível alto
void led_set()
{
    GPIOC->BSRR = (1U << LED_PIN);
}


// Coloca pino em nível baixo
void led_clr()
{
    GPIOC->BSRR = (1U << (LED_PIN + 16U));
}

// LED da BlackPill é ativo em nível baixo
#define LED_ON()   led_clr()
#define LED_OFF()  led_set()

int main(void) {
    usb_serial_init();
    led_init();
    button_interrupt_init();

    while (1) {
        if (button_event != 0U)
        {   delay_ms(DEBOUNCE_MS);
            button_event = 0U;
        }
        printf("\nExemplo uso.\n");
        fflush(stdout);
    }
}