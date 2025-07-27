#pragma once

#include "targets.h"
#include "elrs_eeprom.h"
#include "options.h"
#include "common.h"
#include <nvs_flash.h>
#include <nvs.h>

// CONFIG_MAGIC is ORed with CONFIG_VERSION in the version field
#define CONFIG_MAGIC_MASK   (0b11U << 30)
#define TX_CONFIG_MAGIC     (0b01U << 30)
#define TX_CONFIG_VERSION   10U
#define CONFIG_TX_MODEL_CNT 20U

typedef struct {
    uint32_t    modelMatch:1,
                _unused:31;
} model_config_t;

typedef struct {
    uint32_t        version;
    model_config_t  model_config[CONFIG_TX_MODEL_CNT];
} tx_config_t;

class TxConfig
{
public:
    TxConfig();
    void Load();
    uint32_t Commit();

    // Getters
    bool GetModelMatch() const { return m_model->modelMatch; }

    // Setters
    void SetModelMatch(bool modelMatch);
    void SetDefaults(bool commit);
    void SetStorageProvider(ELRS_EEPROM *eeprom);

    // State setters
    bool SetModelId(uint8_t modelId);

private:
    tx_config_t m_config;
    ELRS_EEPROM *m_eeprom;
    uint32_t     m_modified;
    model_config_t *m_model;
    uint8_t     m_modelId;
    nvs_handle  handle;
};

extern TxConfig config;
