#pragma once

/***
 * Outdated config structs used by the update process
 ***/

#include <inttypes.h>

/***
 * TX config
 ***/

// V5
typedef struct {
    uint8_t     rate:3;
    uint8_t     tlm:3;
    uint8_t     power:3;
    uint8_t     switchMode:2;
    uint8_t     modelMatch:1;
    uint8_t     dynamicPower:1;
    uint8_t     boostChannel:3;
} v5_model_config_t; // 16 bits

typedef struct {
    uint32_t        version;
    v5_model_config_t  model_config[64];
} v5_tx_config_t;

// V6
typedef v5_model_config_t v6_model_config_t;

typedef struct {
    uint32_t        version;
    char            ssid[33];
    char            password[33];
    v6_model_config_t  model_config[64];
} v6_tx_config_t;

// V7
typedef struct {
    uint32_t    rate:4,
                tlm:4,
                power:3,
                switchMode:2,
                boostChannel:3,
                dynamicPower:1,
                modelMatch:1,
                free1:2,
                ptrStartChannel:4,
                ptrEnableChannel:5,
                free2:3;
} v7_model_config_t;

typedef struct {
    uint32_t        version;
    char            ssid[33];
    char            password[33];
    v7_model_config_t  model_config[64];
} v7_tx_config_t;
