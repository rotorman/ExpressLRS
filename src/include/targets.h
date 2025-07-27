#pragma once
#include <Arduino.h>

#define UNDEF_PIN (-1)

/////////////////////////

#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))

#undef ICACHE_RAM_ATTR //fix to allow both esp32 and esp8266 to use ICACHE_RAM_ATTR for mapping to IRAM
#define ICACHE_RAM_ATTR IRAM_ATTR

#include <soc/uart_pins.h>
#include "hardware.h"
