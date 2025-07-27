#include "config.h"
#include "common.h"
#include "device.h"
#include "helpers.h"

#define ALL_CHANGED         (EVENT_CONFIG_MODEL_CHANGED | EVENT_CONFIG_MAIN_CHANGED)

// Really awful but safe(?) type punning of model_config_t to and from uint32_t
template<class T> static const void U32_to_Model(uint32_t const u32, T * const model)
{
    union {
        union {
            T model;
            uint8_t padding[sizeof(uint32_t)-sizeof(T)];
        } val;
        uint32_t u32;
    } converter = { .u32 = u32 };

    *model = converter.val.model;
}

template<class T> static const uint32_t Model_to_U32(T const * const model)
{
    // clear the entire union because the assignment will only fill sizeof(T)
    union {
        union {
            T model;
            uint8_t padding[sizeof(uint32_t)-sizeof(T)];
        } val;
        uint32_t u32;
    } converter = { 0 };

    converter.val.model = *model;
    return converter.u32;
}

TxConfig::TxConfig() :
    m_model(m_config.model_config)
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
    ESP_ERROR_CHECK(nvs_open("ELRS", NVS_READWRITE, &handle));

    // Try to load the version and make sure it is a TX config
    uint32_t version = 0;
    if (nvs_get_u32(handle, "tx_version", &version) == ESP_OK && ((version & CONFIG_MAGIC_MASK) == TX_CONFIG_MAGIC))
        version = version & ~CONFIG_MAGIC_MASK;

    // Can't upgrade from version <5, or when flashing a previous version, just use defaults.
    if (version != TX_CONFIG_VERSION)
    {
        SetDefaults(true);
        return;
    }

    SetDefaults(false);

    uint32_t value;

    for(unsigned i=0; i<CONFIG_TX_MODEL_CNT; i++)
    {
        char model[10] = "model";
        itoa(i, model+5, 10);
        if (nvs_get_u32(handle, model, &value) == ESP_OK)
        {
            if (version == TX_CONFIG_VERSION)
            {
                U32_to_Model(value, &m_config.model_config[i]);
            }
        }
    } // for each model

    if (version != TX_CONFIG_VERSION)
    {
        Commit();
    }
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
        uint32_t value = Model_to_U32(m_model);
        char model[10] = "model";
        itoa(m_modelId, model+5, 10);
        nvs_set_u32(handle, model, value);
    }
    nvs_set_u32(handle, "tx_version", m_config.version);
    nvs_commit(handle);
    uint32_t changes = m_modified;
    m_modified = 0;
    return changes;
}

// Setters
void TxConfig::SetModelMatch(bool modelMatch)
{
    if (GetModelMatch() != modelMatch)
    {
        m_model->modelMatch = modelMatch;
        m_modified |= EVENT_CONFIG_MODEL_CHANGED;
    }
}

void TxConfig::SetStorageProvider(ELRS_EEPROM *eeprom)
{
    if (eeprom)
    {
        m_eeprom = eeprom;
    }
}

void TxConfig::SetDefaults(bool commit)
{
    // Reset everything to 0/false and then just set anything that zero is not appropriate
    memset(&m_config, 0, sizeof(m_config));

    m_config.version = TX_CONFIG_VERSION | TX_CONFIG_MAGIC;
    m_modified = ALL_CHANGED;

    if (commit)
    {
        m_modified = ALL_CHANGED;
    }

    for (unsigned i=0; i<CONFIG_TX_MODEL_CNT; i++)
    {
        SetModelId(i);
        // ESP32 nvs needs to commit every model
        if (commit)
        {
            m_modified |= EVENT_CONFIG_MODEL_CHANGED;
            Commit();
        }
    }

    SetModelId(0);
    m_modified = 0;
}

/**
 * Sets ModelId used for subsequent per-model config gets
 * Returns: true if the model has changed
 **/
bool TxConfig::SetModelId(uint8_t modelId)
{
    model_config_t *newModel = &m_config.model_config[modelId];
    if (newModel != m_model)
    {
        m_model = newModel;
        m_modelId = modelId;
        return true;
    }

    return false;
}
