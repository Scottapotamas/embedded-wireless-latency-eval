#ifndef SUPPORT_H
#define SUPPORT_H

#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_spi.h"

#define NRF_SPI SPI1

static inline void nRF24_CE_L()
{
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_4);
}

static inline void nRF24_CE_H()
{
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_4);
}

static inline void nRF24_CSN_L()
{
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_4);
}

static inline void nRF24_CSN_H()
{
    LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_4);
}


static inline uint8_t nRF24_LL_RW(uint8_t data) {

    LL_SPI_Enable(NRF_SPI);
    // Wait until TX buffer is empty
    while (LL_SPI_IsActiveFlag_BSY(NRF_SPI));
    while (!LL_SPI_IsActiveFlag_TXE(NRF_SPI));
    LL_SPI_TransmitData8(NRF_SPI, data);
    while (!LL_SPI_IsActiveFlag_RXNE(NRF_SPI));
    return LL_SPI_ReceiveData8(NRF_SPI);
}

#endif //SUPPORT_H