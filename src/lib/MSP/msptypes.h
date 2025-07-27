#pragma once

#define MSP_ELRS_FUNC       0x4578 // ['E','x']

#define MSP_SET_RX_CONFIG   45
#define MSP_EEPROM_WRITE    250  //in message          no param

// ELRS specific opcodes
#define MSP_ELRS_RF_MODE                    0x06    // NOTIMPL
#define MSP_ELRS_TX_PWR                     0x07    // NOTIMPL
#define MSP_ELRS_TLM_RATE                   0x08    // NOTIMPL
#define MSP_ELRS_BIND                       0x09
#define MSP_ELRS_MODEL_ID                   0x0A

#define MSP_ELRS_POWER_CALI_GET             0x20
#define MSP_ELRS_POWER_CALI_SET             0x21

// CRSF encapsulated msp defines
#define ENCAPSULATED_MSP_HEADER_CRC_LEN     4
#define ENCAPSULATED_MSP_MAX_PAYLOAD_SIZE   4
#define ENCAPSULATED_MSP_MAX_FRAME_LEN      (ENCAPSULATED_MSP_HEADER_CRC_LEN + ENCAPSULATED_MSP_MAX_PAYLOAD_SIZE)
