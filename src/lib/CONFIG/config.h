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
#define RX_CONFIG_MAGIC     (0b10U << 30)

#define TX_CONFIG_VERSION   8U
#define RX_CONFIG_VERSION   10U

#define CONFIG_TX_BUTTON_ACTION_CNT 2
#define CONFIG_TX_MODEL_CNT         64

typedef enum {
    HT_OFF,
    HT_ON,
    HT_AUX1_UP,
    HT_AUX1_DN,
    HT_AUX2_UP,
    HT_AUX2_DN,
    HT_AUX3_UP,
    HT_AUX3_DN,
    HT_AUX4_UP,
    HT_AUX4_DN,
    HT_AUX5_UP,
    HT_AUX5_DN,
    HT_AUX6_UP,
    HT_AUX6_DN,
    HT_AUX7_UP,
    HT_AUX7_DN,
    HT_AUX8_UP,
    HT_AUX8_DN,
} headTrackingEnable_t;

typedef enum {
    HT_START_EDGETX,
    HT_START_AUX1,
    HT_START_AUX2,
    HT_START_AUX3,
    HT_START_AUX4,
    HT_START_AUX5,
    HT_START_AUX6,
    HT_START_AUX7,
    HT_START_AUX8,
    HT_START_AUX9,
    HT_START_AUX10,
} headTrackingStart_t;

typedef struct {
    uint32_t    rate:5,
                tlm:4,
                power:3,
                switchMode:2,
                boostChannel:3, // dynamic power boost AUX channel
                dynamicPower:1,
                modelMatch:1,
                txAntenna:2,    // FUTURE: Which TX antenna to use, 0=Auto
                ptrStartChannel:4,
                ptrEnableChannel:5,
                free:2;
} model_config_t;

typedef struct {
    uint8_t     pressType:1,    // 0 short, 1 long
                count:3,        // 1-8 click count for short, .5sec hold count for long
                action:4;       // action to execute
} button_action_t;

typedef union {
    struct {
        uint8_t color;                  // RRRGGGBB
        button_action_t actions[CONFIG_TX_BUTTON_ACTION_CNT];
        uint8_t unused;
    } val;
    uint32_t raw;
} tx_button_color_t;

typedef struct {
    uint32_t        version;
    uint8_t         powerFanThreshold:4; // Power level to enable fan if present
    model_config_t  model_config[CONFIG_TX_MODEL_CNT];
    uint8_t         fanMode;            // some value used by thermal?
    uint8_t         motionMode:2,       // bool, but space for 2 more modes
                    free:6;
    tx_button_color_t buttonColors[2];  // FUTURE: TX RGB color / mode (sets color of TX, can be a static color or standard)
                                        // FUTURE: Model RGB color / mode (sets LED color mode on the model, but can be second TX led color too)
                                        // FUTURE: Custom button actions
} tx_config_t;

class TxConfig
{
public:
    TxConfig();
    void Load();
    uint32_t Commit();

    // Getters
    uint8_t GetRate() const { return m_model->rate; }
    uint8_t GetTlm() const { return m_model->tlm; }
    uint8_t GetPower() const { return m_model->power; }
    bool GetDynamicPower() const { return m_model->dynamicPower; }
    uint8_t GetBoostChannel() const { return m_model->boostChannel; }
    uint8_t GetSwitchMode() const { return m_model->switchMode; }
    uint8_t GetAntennaMode() const { return m_model->txAntenna; }
    bool GetModelMatch() const { return m_model->modelMatch; }
    bool     IsModified() const { return m_modified != 0; }
    uint8_t GetPowerFanThreshold() const { return m_config.powerFanThreshold; }
    uint8_t  GetFanMode() const { return m_config.fanMode; }
    uint8_t  GetMotionMode() const { return m_config.motionMode; }
    tx_button_color_t const *GetButtonActions(uint8_t button) const { return &m_config.buttonColors[button]; }
    model_config_t const &GetModelConfig(uint8_t model) const { return m_config.model_config[model]; }
    uint8_t GetPTRStartChannel() const { return m_model->ptrStartChannel; }
    uint8_t GetPTREnableChannel() const { return m_model->ptrEnableChannel; }

    // Setters
    void SetRate(uint8_t rate);
    void SetTlm(uint8_t tlm);
    void SetPower(uint8_t power);
    void SetDynamicPower(bool dynamicPower);
    void SetBoostChannel(uint8_t boostChannel);
    void SetSwitchMode(uint8_t switchMode);
    void SetAntennaMode(uint8_t txAntenna);
    void SetModelMatch(bool modelMatch);
    void SetDefaults(bool commit);
    void SetStorageProvider(ELRS_EEPROM *eeprom);
    void SetPowerFanThreshold(uint8_t powerFanThreshold);
    void SetFanMode(uint8_t fanMode);
    void SetMotionMode(uint8_t motionMode);
    void SetButtonActions(uint8_t button, tx_button_color_t actions[2]);
    void SetPTRStartChannel(uint8_t ptrStartChannel);
    void SetPTREnableChannel(uint8_t ptrEnableChannel);

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
