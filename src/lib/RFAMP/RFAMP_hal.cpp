#include "RFAMP_hal.h"

RFAMP_hal *RFAMP_hal::instance = NULL;

RFAMP_hal::RFAMP_hal()
{
    instance = this;
}

void RFAMP_hal::init()
{
    #define SET_BIT(n) ((n != UNDEF_PIN) ? 1ULL << n : 0)

    txrx_disable_clr_bits = 0;
    txrx_disable_clr_bits |= SET_BIT(GPIO_PIN_PA_ENABLE);
    txrx_disable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE);
    txrx_disable_clr_bits |= SET_BIT(GPIO_PIN_TX_ENABLE);
    txrx_disable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE_2);
    txrx_disable_clr_bits |= SET_BIT(GPIO_PIN_TX_ENABLE_2);

    tx1_enable_set_bits = 0;
    tx1_enable_clr_bits = 0;
    tx1_enable_set_bits |= SET_BIT(GPIO_PIN_PA_ENABLE);
    tx1_enable_set_bits |= SET_BIT(GPIO_PIN_TX_ENABLE);
    tx1_enable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE);
    tx1_enable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE_2);

    tx2_enable_set_bits = 0;
    tx2_enable_clr_bits = 0;
    tx2_enable_set_bits |= SET_BIT(GPIO_PIN_PA_ENABLE);
    tx2_enable_set_bits |= SET_BIT(GPIO_PIN_TX_ENABLE_2);
    tx2_enable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE_2);
    tx2_enable_clr_bits |= SET_BIT(GPIO_PIN_RX_ENABLE);

    tx_all_enable_set_bits = 0;
    tx_all_enable_clr_bits = 0; 
    tx_all_enable_set_bits = tx1_enable_set_bits | tx2_enable_set_bits;
    tx_all_enable_clr_bits = tx1_enable_clr_bits | tx2_enable_clr_bits; 

    rx_enable_set_bits = 0;
    rx_enable_clr_bits = 0;
    rx_enable_set_bits |= SET_BIT(GPIO_PIN_PA_ENABLE);
    rx_enable_set_bits |= SET_BIT(GPIO_PIN_RX_ENABLE);
    rx_enable_set_bits |= SET_BIT(GPIO_PIN_RX_ENABLE_2);
    rx_enable_clr_bits |= SET_BIT(GPIO_PIN_TX_ENABLE);
    rx_enable_clr_bits |= SET_BIT(GPIO_PIN_TX_ENABLE_2);

    if (GPIO_PIN_PA_ENABLE != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_PA_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_PA_ENABLE, LOW);
    }

    if (GPIO_PIN_TX_ENABLE != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_TX_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_TX_ENABLE, LOW);
    }

    if (GPIO_PIN_RX_ENABLE != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_RX_ENABLE, OUTPUT);
        digitalWrite(GPIO_PIN_RX_ENABLE, LOW);
    }

    if (GPIO_PIN_TX_ENABLE_2 != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_TX_ENABLE_2, OUTPUT);
        digitalWrite(GPIO_PIN_TX_ENABLE_2, LOW);
    }

    if (GPIO_PIN_RX_ENABLE_2 != UNDEF_PIN)
    {
        pinMode(GPIO_PIN_RX_ENABLE_2, OUTPUT);
        digitalWrite(GPIO_PIN_RX_ENABLE_2, LOW);
    }
}

void ICACHE_RAM_ATTR RFAMP_hal::TXenable(SX12XX_Radio_Number_t radioNumber)
{
#if defined(PLATFORM_ESP32_C3)
    if (radioNumber == SX12XX_Radio_All)
    {
        GPIO.out_w1ts.out_w1ts = tx_all_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx_all_enable_clr_bits;
    }
    else if (radioNumber == SX12XX_Radio_2)
    {
        GPIO.out_w1ts.out_w1ts = tx2_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx2_enable_clr_bits;
    }
    else
    {
        GPIO.out_w1ts.out_w1ts = tx1_enable_set_bits;
        GPIO.out_w1tc.out_w1tc = tx1_enable_clr_bits;
    }
#else
    if (radioNumber == SX12XX_Radio_All)
    {
        GPIO.out_w1ts = (uint32_t)tx_all_enable_set_bits;
        GPIO.out_w1tc = tx_all_enable_clr_bits;

        GPIO.out1_w1ts.data = tx_all_enable_set_bits >> 32;
        GPIO.out1_w1tc.data = tx_all_enable_clr_bits >> 32;
    }
    else if (radioNumber == SX12XX_Radio_2)
    {
        GPIO.out_w1ts = tx2_enable_set_bits;
        GPIO.out_w1tc = tx2_enable_clr_bits;

        GPIO.out1_w1ts.data = tx2_enable_set_bits >> 32;
        GPIO.out1_w1tc.data = tx2_enable_clr_bits >> 32;
    }
    else
    {
        GPIO.out_w1ts = tx1_enable_set_bits;
        GPIO.out_w1tc = tx1_enable_clr_bits;

        GPIO.out1_w1ts.data = tx1_enable_set_bits >> 32;
        GPIO.out1_w1tc.data = tx1_enable_clr_bits >> 32;
    }
#endif
}

void ICACHE_RAM_ATTR RFAMP_hal::RXenable()
{
#if defined(PLATFORM_ESP32_C3)
    GPIO.out_w1ts.out_w1ts = rx_enable_set_bits;
    GPIO.out_w1tc.out_w1tc = rx_enable_clr_bits;
#else
    GPIO.out_w1ts = rx_enable_set_bits;
    GPIO.out_w1tc = rx_enable_clr_bits;

    GPIO.out1_w1ts.data = rx_enable_set_bits >> 32;
    GPIO.out1_w1tc.data = rx_enable_clr_bits >> 32;
#endif
}

void ICACHE_RAM_ATTR RFAMP_hal::TXRXdisable()
{
#if defined(PLATFORM_ESP32_C3)
    GPIO.out_w1tc.out_w1tc = txrx_disable_clr_bits;
#else
    GPIO.out_w1tc = txrx_disable_clr_bits;
    GPIO.out1_w1tc.data = txrx_disable_clr_bits >> 32;
#endif
}
