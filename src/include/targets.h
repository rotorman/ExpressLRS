#pragma once
#include <Arduino.h>

#define UNDEF_PIN (-1)

/// General Features ///
#define LED_MAX_BRIGHTNESS 50 //0..255 for max led brightness

/////////////////////////

#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define WORD_PADDED(size) (((size)+3) & ~3)

#undef ICACHE_RAM_ATTR //fix to allow both esp32 and esp8266 to use ICACHE_RAM_ATTR for mapping to IRAM
#define ICACHE_RAM_ATTR IRAM_ATTR

/*
 * Features
 * define features based on pins before defining pins as UNDEF_PIN
 */

#if defined(DEBUG_CRSF_NO_OUTPUT)
#define OPT_CRSF_RCVR_NO_SERIAL true
#else
#define OPT_CRSF_RCVR_NO_SERIAL false
#endif

#if defined(RADIO_SX128X)
#define Regulatory_Domain_ISM_2400 1
// ISM 2400 band is in use => undefine other requlatory domain defines
#undef Regulatory_Domain_AU_915
#undef Regulatory_Domain_EU_868
#undef Regulatory_Domain_IN_866
#undef Regulatory_Domain_FCC_915
#undef Regulatory_Domain_AU_433
#undef Regulatory_Domain_EU_433
#undef Regulatory_Domain_US_433
#undef Regulatory_Domain_US_433_WIDE

#elif defined(RADIO_SX127X) || defined(RADIO_LR1121)
#if !(defined(Regulatory_Domain_AU_915) || defined(Regulatory_Domain_FCC_915) || \
        defined(Regulatory_Domain_EU_868) || defined(Regulatory_Domain_IN_866) || \
        defined(Regulatory_Domain_AU_433) || defined(Regulatory_Domain_EU_433) || \
        defined(Regulatory_Domain_US_433) || defined(Regulatory_Domain_US_433_WIDE))
#error "Regulatory_Domain is not defined for 900MHz device. Check user_defines.txt!"
#endif
#else
#error "Either RADIO_SX127X, RADIO_LR1121 or RADIO_SX128X must be defined!"
#endif

#include <soc/uart_pins.h>
#if !defined(U0RXD_GPIO_NUM)
#define U0RXD_GPIO_NUM (3)
#endif
#if !defined(U0TXD_GPIO_NUM)
#define U0TXD_GPIO_NUM (1)
#endif

#include "hardware.h"
