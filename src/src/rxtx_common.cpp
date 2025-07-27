#include "targets.h"
#include "common.h"
#include "config.h"
#include <functional>
#include <Wire.h>

boolean i2c_enabled = false;

static void setupWire()
{
    int gpio_scl = GPIO_PIN_SCL;
    int gpio_sda = GPIO_PIN_SDA;

    if(gpio_sda != UNDEF_PIN && gpio_scl != UNDEF_PIN)
    {
        // ESP hopes to get Wire::begin(int, int)
        // ESP32 hopes to get Wire::begin(int = -1, int = -1, uint32 = 0)
        Wire.begin(gpio_sda, gpio_scl);
        Wire.setClock(400000);
        i2c_enabled = true;
    }
}

void setupTargetCommon()
{
    setupWire();
}
