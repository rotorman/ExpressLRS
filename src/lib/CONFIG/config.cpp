#include "config.h"
#include "common.h"
#include "device.h"
#include "helpers.h"

#define ALL_CHANGED         (EVENT_CONFIG_MODEL_CHANGED | EVENT_CONFIG_MAIN_CHANGED)

extern void luaUpdateMAC();

TxConfig::TxConfig()
{
}

void TxConfig::Load()
{
    m_modified = 0;

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(nvs_open("ELRS-ESPNOW", NVS_READWRITE, &handle));

    SetModelId(0);
    m_modified = 0;

    for(unsigned i=0; i<CONFIG_TX_MODEL_CNT; i++)
    {
        char model[10] = "model";
        itoa(i, model+5, 10);
        nvs_get_u64(handle, model, &m_model_mac[i]);
    } // for each model
}

uint32_t TxConfig::Commit()
{
    if (!m_modified)
    {
        // No changes
        return 0;
    }
    // Write parts to NVS
    if (m_modified & EVENT_CONFIG_MODEL_CHANGED)
    {
        char model[10] = "model";
        itoa(m_modelId, model+5, 10);
        nvs_set_u64(handle, model, m_model_mac[m_modelId]);
        nvs_commit(handle);
    }
    uint32_t changes = m_modified;
    m_modified = 0;
    return changes;
}

// Setters
void TxConfig::SetStorageProvider(ELRS_EEPROM *eeprom)
{
    if (eeprom)
    {
        m_eeprom = eeprom;
    }
}

void TxConfig::SetModelMAC(uint64_t mac)
{
    m_model_mac[m_modelId] = mac;
    m_modified |= EVENT_CONFIG_MODEL_CHANGED;
    luaUpdateMAC();
}

/**
 * Sets ModelId used for subsequent per-model config gets
 * Returns: true if the model has changed
 **/
bool TxConfig::SetModelId(uint8_t modelId)
{
    if (m_modelId != modelId)
    {
        m_modelId = modelId;
        return true;
    }

    return false;
}
