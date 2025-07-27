#pragma once

#include "targets.h"

extern const unsigned char target_name[];
extern const uint8_t target_name_size;
extern const char commit[];
extern const char version[];

enum BuzzerMode {
    buzzerQuiet,
    buzzerOne,
    buzzerTune
};

typedef struct _options {
    uint8_t     _magic_[8];     // this is the magic constant so the configurator can find this options block
    uint16_t    _version_;      // the version of this structure
    uint8_t     hasUID;
    uint8_t     uid[6];         // MY_UID derived from MY_BINDING_PHRASE
    uint32_t    flash_discriminator;    // Discriminator value used to determine if the device has been reflashed and therefore
                                        // the SPIFSS settings are obsolete and the flashed settings should be used in preference
} __attribute__((packed)) firmware_options_t;

// Layout is PRODUCTNAME DEVICENAME OPTIONS HARDWARE
constexpr size_t ELRSOPTS_PRODUCTNAME_SIZE = 128;
constexpr size_t ELRSOPTS_DEVICENAME_SIZE = 16;
constexpr size_t ELRSOPTS_OPTIONS_SIZE = 512;
constexpr size_t ELRSOPTS_HARDWARE_SIZE = 2048;

extern firmware_options_t firmwareOptions;
extern bool options_init();

extern char product_name[];
extern char device_name[];
extern String& getOptions();
extern String& getHardware();
extern void saveOptions();

#include "EspFlashStream.h"
bool options_HasStringInFlash(EspFlashStream &strmFlash);
void options_SetTrueDefaults();
