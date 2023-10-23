/* -------------------------------------------------------------------------- */

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "stm32f4xx_ll_cortex.h"
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_system.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_pwr.h>
#include <stm32f4xx_ll_utils.h>

#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_exti.h"


/* -------------------------------------------------------------------------- */

#include "uart.h"

/* -------------------------------------------------------------------------- */

#if defined(UART_POLL)
    // TODO

#elif defined(UART_IRQ)
    // TODO

#elif defined(UART_DMA)
    // TODO

#else
    // Fallback to the DMA implementation if unspecifed
    // Define one of UART_POLL or UART_IRQ or UART_DMA in the CMakeLists please
    #define UART_DMA
#endif

/* -------------------------------------------------------------------------- */

void hal_core_init( void );
void hal_core_clock_configure( void );
void portAssertHandler( const char *file,
                        unsigned    line,
                        const char *fmt,
                        ... );

/* -------------------------------------------------------------------------- */

void setup_gpio_output( void );
void setup_gpio_input( void );

volatile bool trigger_pending = false;

uint8_t short_string[12] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE,
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    hal_core_init();
    hal_core_clock_configure();

    setup_gpio_output();
    setup_gpio_input();

    uart_init();

    volatile uint8_t rx_tmp[32] = {0};
    uint8_t bytes_held = 0;

    while(1)
    {

        if( hal_uart_rx_data_available() )
        {
            bytes_held = hal_uart_read( (uint8_t*)&rx_tmp, 32 );

            for(uint8_t i = 0; i < bytes_held; i++)
            {
                if(rx_tmp[i] == 0xEE )
                {
                    LL_GPIO_SetOutputPin( GPIOB, LL_GPIO_PIN_0 );
                }
            }
            LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
        }

        if(trigger_pending)
        {
            // GPIO high
//            LL_GPIO_SetOutputPin( GPIOB, LL_GPIO_PIN_0);
            hal_uart_write( short_string, sizeof(short_string) );
//            LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
            trigger_pending = false;
        }
        else
        {
            // GPIO low
//            LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

void setup_gpio_output( void )
{
    LL_PWR_DisableWakeUpPin( LL_PWR_WAKEUP_PIN1 );

    // PB0
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOB );

    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_0, LL_GPIO_SPEED_FREQ_LOW );
    LL_GPIO_SetPinOutputType( GPIOB, LL_GPIO_PIN_0, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_0, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
}

/* -------------------------------------------------------------------------- */

void setup_gpio_input( void )
{
    // PA0 as input
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOA );

    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_0, LL_GPIO_SPEED_FREQ_LOW );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_0, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOA, LL_GPIO_PIN_0 );

    // EXTI0 setup
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_0);

    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE0);

    // IRQ config
    NVIC_SetPriority(EXTI0_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(),
            0,
            0
            ));
    NVIC_EnableIRQ(EXTI0_IRQn);

}

/* -------------------------------------------------------------------------- */

void SysTick_Handler(void)
{

}

void EXTI0_IRQHandler(void)
{
    if(LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_0))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_0);
        trigger_pending = true;
    }
}

// UART5 IRQ functions are in uart.c

/* -------------------------------------------------------------------------- */

void hal_core_init( void )
{
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
    LL_FLASH_EnablePrefetch();

    LL_APB2_GRP1_EnableClock( LL_APB2_GRP1_PERIPH_SYSCFG );
    LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_PWR );

    NVIC_SetPriority( MemoryManagement_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( BusFault_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( UsageFault_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( SVCall_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( DebugMonitor_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( PendSV_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( SysTick_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
}


// Startup the internal and external clocks, set PLL etc
void hal_core_clock_configure( void )
{
    LL_FLASH_SetLatency( LL_FLASH_LATENCY_5 );

    if( LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5 )
    {
        portAssertHandler("main.c", __LINE__, 0);
    }

    LL_PWR_SetRegulVoltageScaling( LL_PWR_REGU_VOLTAGE_SCALE1 );
    LL_PWR_DisableOverDriveMode();
    LL_RCC_HSE_EnableBypass();

    LL_RCC_HSE_Enable();
    while( LL_RCC_HSE_IsReady() != 1 )
    {
    }

//    LL_RCC_LSI_Enable();
//    while( LL_RCC_LSI_IsReady() != 1 )
//    {
//    }

    LL_RCC_PLL_ConfigDomain_SYS( LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, LL_RCC_PLLP_DIV_2 );
    LL_RCC_PLL_ConfigDomain_48M( LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, LL_RCC_PLLQ_DIV_7 );
    LL_RCC_PLL_Enable();
    while( LL_RCC_PLL_IsReady() != 1 )
    {
    }
    while( LL_PWR_IsActiveFlag_VOS() == 0 )
    {
    }

    LL_RCC_SetAHBPrescaler( LL_RCC_SYSCLK_DIV_1 );
    LL_RCC_SetAPB1Prescaler( LL_RCC_APB1_DIV_4 );
    LL_RCC_SetAPB2Prescaler( LL_RCC_APB2_DIV_2 );

    LL_RCC_SetSysClkSource( LL_RCC_SYS_CLKSOURCE_PLL );
    while( LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL )
    {
    }

    LL_Init1msTick( 168000000 );
    LL_SetSystemCoreClock( 168000000 );
//    LL_RCC_SetTIMPrescaler( LL_RCC_TIM_PRESCALER_TWICE );

    LL_SYSTICK_EnableIT();
}

void portAssertHandler( const char *file,
                        unsigned    line,
                        const char *fmt,
                        ... )
{
    va_list  args;

    // Forward directly to the 'in-memory cache' handler function
    va_start( args, fmt );
    // Read/handle file/line strings here
    va_end( args );

    // Wait for the watch dog to bite
    for( ;; )
    {
        asm("NOP");
    }

}

/* -------------------------------------------------------------------------- */