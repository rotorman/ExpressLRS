#pragma once

#include "targets.h"
#include "elrs_eeprom.h"
#include "options.h"
#include "common.h"
#include <nvs_flash.h>
#include <nvs.h>

#define CONFIG_TX_MODEL_CNT 64U // max supported model count of EdgeTX

class TxConfig
{
public:
    TxConfig();
    void Load();
    uint32_t Commit();

    // Getters
    uint64_t GetModelMAC() const { return m_model_mac[m_modelId]; }
    bool     IsModified() const { return m_modified != 0; }

    // Setters
    void SetStorageProvider(ELRS_EEPROM *eeprom);
    void SetModelMAC(uint64_t mac);

    // State setters
    bool SetModelId(uint8_t modelId);

private:
    ELRS_EEPROM *m_eeprom;
    uint32_t     m_modified;
    uint64_t     m_model_mac[CONFIG_TX_MODEL_CNT];
    uint8_t      m_modelId;
    nvs_handle   handle;
};

extern TxConfig config;
