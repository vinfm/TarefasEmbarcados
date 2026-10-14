#include <stdint.h>
#include <stdio.h>

#include "stm32f4xx.h"
#include "tusb.h"
#include "usb_serial.h"

static uint32_t g_led_last = 0;
volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void)
{
    ms_ticks++;
}

void usb_serial_task(void)
{
    tud_task();
}

uint32_t tusb_time_millis_api(void)
{
    return ms_ticks;
}

void tusb_time_delay_ms_api(uint32_t ms)
{
    uint32_t start = ms_ticks;

    while ((uint32_t)(ms_ticks - start) < ms)
    {
        __NOP();
    }
}

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSION;

    while ((RCC->CR & RCC_CR_HSIRDY) == 0U)
    {
    }

    RCC->CFGR &= ~RCC_CFGR_SW;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
    {
    }

    RCC->CR &= ~RCC_CR_PLLON;

    while ((RCC->CR & RCC_CR_PLLRDY) != 0U)
    {
    }

    RCC->CR |= RCC_CR_HSEON;

    while ((RCC->CR & RCC_CR_HSERDY) == 0U)
    {
    }

    RCC->APB1ENR |= RCC_APB1ENR_PWREN;

#ifdef PWR_CR_VOS
    PWR->CR |= PWR_CR_VOS;
#endif

    FLASH->ACR =
        FLASH_ACR_ICEN |
        FLASH_ACR_DCEN |
        FLASH_ACR_PRFTEN |
        FLASH_ACR_LATENCY_2WS;

    // 84 MHz para CPU e 48 MHz para USB
    RCC->PLLCFGR =
        (25U  << 0U) |
        (336U << 6U) |
        (1U   << 16U) |
        RCC_PLLCFGR_PLLSRC_HSE |
        (7U   << 24U);

    RCC->CFGR &= ~(
        RCC_CFGR_HPRE |
        RCC_CFGR_PPRE1 |
        RCC_CFGR_PPRE2
    );

    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;

    RCC->CR |= RCC_CR_PLLON;

    while ((RCC->CR & RCC_CR_PLLRDY) == 0U)
    {
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
    }

    SystemCoreClockUpdate();
}

static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    __DSB();

    GPIOC->MODER &= ~(3U << (13U * 2U));
    GPIOC->MODER |=  (1U << (13U * 2U));
    GPIOC->OTYPER &= ~(1U << 13U);
    GPIOC->BSRR = (1U << 13U);
}

static void led_task(void)
{
    if ((uint32_t)(ms_ticks - g_led_last) >= 1000U)
    {
        g_led_last = ms_ticks;
        GPIOC->ODR ^= (1U << 13U);
    }
}

static void usb_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    __DSB();

    // PA11 = DM e PA12 = DP
    GPIOA->MODER &= ~(
        (3U << (11U * 2U)) |
        (3U << (12U * 2U))
    );

    GPIOA->MODER |=
        (2U << (11U * 2U)) |
        (2U << (12U * 2U));

    GPIOA->OTYPER &= ~(
        (1U << 11U) |
        (1U << 12U)
    );

    GPIOA->OSPEEDR &= ~(
        (3U << (11U * 2U)) |
        (3U << (12U * 2U))
    );

    GPIOA->OSPEEDR |=
        (3U << (11U * 2U)) |
        (3U << (12U * 2U));

    GPIOA->PUPDR &= ~(
        (3U << (11U * 2U)) |
        (3U << (12U * 2U))
    );

    GPIOA->AFR[1] &= ~(
        (0xFU << 12U) |
        (0xFU << 16U)
    );

    GPIOA->AFR[1] |=
        (10U << 12U) |
        (10U << 16U);

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    __DSB();
}

void OTG_FS_IRQHandler(void)
{
    tusb_int_handler(0, true);
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if (!tud_cdc_connected())
    {
        return len;
    }

    uint32_t done = 0;

    while (done < (uint32_t)len)
    {
        uint32_t n = tud_cdc_write(ptr + done, (uint32_t)len - done);
        done += n;
        tud_cdc_write_flush();

        if (n == 0U)
        {
            tud_task();
        }
    }

    return len;
}

void usb_serial_init(void)
{
    clock_init();
    led_init();

    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        while (1)
        {
        }
    }

    usb_gpio_init();

    tusb_rhport_init_t usb_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    if (!tusb_rhport_init(0, &usb_init))
    {
        while (1)
        {
        }
    }

    NVIC_SetPriority(OTG_FS_IRQn, 2U);
    NVIC_EnableIRQ(OTG_FS_IRQn);

    // Espera o terminal abrir a CDC
    while (!tud_cdc_connected())
    {
        tud_task();
    }

    for (volatile uint32_t i = 0; i < 500000U; ++i)
    {
        tud_task();
    }
}

int usb_serial_getchar(void)
{
    tud_task();

    if (!tud_cdc_available())
    {
        return -1;
    }

    uint8_t ch;

    if (tud_cdc_read(&ch, 1U) != 1U)
    {
        return -1;
    }

    return (int)ch;
}
