#include "OLED/oleddisplay.h"
#include "TFT/tftdisplay.h"

#include "common.h"
#include "config.h"
#include "helpers.h"
#include "POWERMGNT.h"
#include "handset.h"
#include "OTA.h"
#include "deferred.h"

extern FiniteStateMachine state_machine;

extern bool RxWiFiReadyToSend;
extern void ResetPower();
extern void setWifiUpdateMode();
extern void SetSyncSpam();
extern uint8_t adjustPacketRateForBaud(uint8_t rate);
extern uint8_t adjustSwitchModeForAirRate(OtaSwitchMode_e eSwitchMode, uint8_t packetSize);

extern Display *display;

extern unsigned long rebootTime;

fsm_state_t getInitialState()
{
    if(esp_reset_reason() == ESP_RST_SW)
    {
        return STATE_IDLE;
    }
    return STATE_SPLASH;
}

static void displaySplashScreen(bool init)
{
    display->displaySplashScreen();
}

static void displayIdleScreen(bool init)
{
    static message_index_t last_message = MSG_INVALID;
    static uint8_t last_rate = 0xFF;
    static uint8_t last_power = 0xFF;
    static uint8_t last_tlm = 0xFF;
    static uint8_t last_dynamic = 0xFF;
    static uint8_t last_run_power = 0xFF;

    uint8_t changed = init ? CHANGED_ALL : 0;
    message_index_t disp_message;
    if (connectionState == noCrossfire || connectionState > FAILURE_STATES) {
        disp_message = MSG_ERROR;
    } else if(handset->IsArmed()) {
        disp_message = MSG_ARMED;
    } else if(connectionState == connected) {
        if (connectionHasModelMatch) {
            disp_message = MSG_CONNECTED;
        } else {
            disp_message = MSG_MISMATCH;
        }
    } else {
        disp_message = MSG_NONE;
    }

    // compute log2(ExpressLRS_currTlmDenom) (e.g. 128=7, 64=6, etc)
    uint8_t tlmIdx = __builtin_ffs(ExpressLRS_currTlmDenom) - 1;
    if (changed == 0)
    {
        changed |= last_message != disp_message ? CHANGED_ALL : 0;
        changed |= last_rate != config.GetRate() ? CHANGED_RATE : 0;
        changed |= last_power != config.GetPower() ? CHANGED_POWER : 0;
        changed |= last_dynamic != config.GetDynamicPower() ? CHANGED_POWER : 0;
        changed |= last_run_power != (uint8_t)(POWERMGNT::currPower()) ? CHANGED_POWER : 0;
        changed |= last_tlm != tlmIdx ? CHANGED_TELEMETRY : 0;
    }

    if (changed)
    {
        last_message = disp_message;
        last_rate = config.GetRate();
        last_power = config.GetPower();
        last_tlm = tlmIdx;
        last_dynamic = config.GetDynamicPower();
        last_run_power = (uint8_t)(POWERMGNT::currPower());

        display->displayIdleScreen(changed, last_rate, last_power, last_tlm, last_dynamic, last_run_power, last_message);
    }
}

static void displayMenuScreen(bool init)
{
    display->displayMainMenu((menu_item_t)state_machine.getCurrentState());
}

// Value menu
static int values_min;
static int values_max;
static int values_index;

static void setupValueIndex(bool init)
{
    switch (state_machine.getParentState())
    {
    case STATE_PACKET:
        values_min = 0;
        values_max = display->getValueCount((menu_item_t)state_machine.getParentState())-1;
        values_index = config.GetRate();
        break;
    case STATE_SWITCH:
        values_min = 0;
        values_max = display->getValueCount((menu_item_t)state_machine.getParentState())-1;
        values_index = config.GetSwitchMode();
        break;
    case STATE_TELEMETRY:
        values_min = 0;
        values_max = display->getValueCount((menu_item_t)state_machine.getParentState())-1;
        values_index = config.GetTlm();
        break;

    case STATE_POWER_MAX:
        values_min = MinPower;
        values_max = POWERMGNT::getMaxPower();
        values_index = config.GetPower();
        break;
    case STATE_POWER_DYNAMIC:
        values_min = 0;
        values_max = display->getValueCount((menu_item_t)state_machine.getParentState())-1;
        values_index = config.GetDynamicPower() ? config.GetBoostChannel() + 1 : 0;
        break;
    }
}

static void displayValueIndex(bool init)
{
    display->displayValue((menu_item_t)state_machine.getParentState(), values_index);
}

static void incrementValueIndex(bool init)
{
    uint8_t values_count = values_max - values_min + 1;
    values_index = (values_index - values_min + 1) % values_count + values_min;
    if (state_machine.getParentState() == STATE_PACKET)
    {
        while (get_elrs_airRateConfig(values_index)->interval < handset->getMinPacketInterval() || !isSupportedRFRate(values_index))
        {
            values_index = (values_index - values_min + 1) % values_count + values_min;
        }
    }
}

static void decrementValueIndex(bool init)
{
    uint8_t values_count = values_max - values_min + 1;
    values_index = (values_index - values_min + values_count - 1) % values_count + values_min;
    if (state_machine.getParentState() == STATE_PACKET)
    {
        while (get_elrs_airRateConfig(values_index)->interval < handset->getMinPacketInterval() || !isSupportedRFRate(values_index))
        {
            values_index = (values_index - values_min + values_count - 1) % values_count + values_min;
        }
    }
}

static void saveValueIndex(bool init)
{
    auto val = values_index;
    switch (state_machine.getParentState())
    {
        case STATE_PACKET: {
            uint8_t actualRate = adjustPacketRateForBaud(val);
            uint8_t newSwitchMode = adjustSwitchModeForAirRate(
                (OtaSwitchMode_e)config.GetSwitchMode(), get_elrs_airRateConfig(actualRate)->PayloadLength);
            // Force Gemini when using dual band modes.
            uint8_t newAntennaMode = 0;
            // If the switch mode is going to change, block the change while connected
            if (newSwitchMode == OtaSwitchModeCurrent || connectionState == disconnected)
            {
                deferExecutionMillis(100, [actualRate, newSwitchMode, newAntennaMode](){
                    config.SetRate(actualRate);
                    config.SetSwitchMode(newSwitchMode);
                    OtaUpdateSerializers((OtaSwitchMode_e)newSwitchMode, ExpressLRS_currAirRate_Modparams->PayloadLength);
                    SetSyncSpam();
                });
            }
            break;
        }
        case STATE_SWITCH: {
            // Only allow changing switch mode when disconnected since we need to guarantee
            // the pack and unpack functions are matched
            if (connectionState == disconnected)
            {
                deferExecutionMillis(100, [val](){
                    config.SetSwitchMode(val);
                    OtaUpdateSerializers((OtaSwitchMode_e)val, ExpressLRS_currAirRate_Modparams->PayloadLength);
                    SetSyncSpam();
                });
            }
            break;
        }
        case STATE_TELEMETRY:
            deferExecutionMillis(100, [val](){
                config.SetTlm(val);
                SetSyncSpam();
            });
            break;

        case STATE_POWER_MAX:
            config.SetPower(values_index);
            if (!config.IsModified())
            {
                ResetPower();
            }
            break;
        case STATE_POWER_DYNAMIC:
            config.SetDynamicPower(values_index > 0);
            config.SetBoostChannel((values_index - 1) > 0 ? values_index - 1 : 0);
            break;
        default:
            break;
    }
}

// WiFi
static void displayWiFiConfirm(bool init)
{
    display->displayWiFiConfirm();
}

static void exitWiFi(bool init)
{
    if (connectionState == wifiUpdate) {
        rebootTime = millis() + 200;
    }
}

static void executeWiFi(bool init)
{
    bool running;
    if (init)
    {
        switch (state_machine.getParentState())
        {
            case STATE_WIFI_TX:
                setWifiUpdateMode();
                break;
            case STATE_WIFI_RX:
                RxWiFiReadyToSend = true;
                break;
        }
        if (state_machine.getParentState() == STATE_WIFI_TX)
        {
            display->displayWiFiStatus();
        }
        else
        {
            display->displayRunning();
        }
        return;
    }
    switch (state_machine.getParentState())
    {
        case STATE_WIFI_TX:
            running = connectionState == wifiUpdate;
            if (running)
            {
                display->displayWiFiStatus();
            }
            break;
        case STATE_WIFI_RX:
            running = RxWiFiReadyToSend;
            break;
        default:
            running = false;
    }
    if (!running)
    {
        state_machine.popState();
    }
}

// Bind
static void displayBindConfirm(bool init)
{
    display->displayBindConfirm();
}

static void executeBind(bool init)
{
    if (init)
    {
        EnterBindingModeSafely();
        display->displayBindStatus();
        return;
    }
    if (!InBindingMode)
    {
        state_machine.popState();
    }
}

// Linkstats
static void displayLinkstats(bool init)
{
    display->displayLinkstats();
}

//-------------------------------------------------------------------

#define MENU_EVENTS(fsm) \
    {EVENT_TIMEOUT, ACTION_POPALL}, \
    {EVENT_LEFT, ACTION_POP}, \
    {EVENT_ENTER, PUSH(fsm)}, \
    {EVENT_RIGHT, PUSH(fsm)}, \
    {EVENT_UP, ACTION_PREVIOUS}, \
    {EVENT_DOWN, ACTION_NEXT}


// Value submenu FSM
fsm_state_event_t const value_init_events[] = {{EVENT_IMMEDIATE, GOTO(STATE_VALUE_SELECT)}};
fsm_state_event_t const value_select_events[] = {
    {EVENT_TIMEOUT, ACTION_POPALL},
    {EVENT_LEFT, ACTION_POP},
    {EVENT_ENTER, GOTO(STATE_VALUE_SAVE)},
    {EVENT_UP, GOTO(STATE_VALUE_DEC)},
    {EVENT_DOWN, GOTO(STATE_VALUE_INC)}
};
fsm_state_event_t const value_increment_events[] = {{EVENT_IMMEDIATE, GOTO(STATE_VALUE_SELECT)}};
fsm_state_event_t const value_decrement_events[] = {{EVENT_IMMEDIATE, GOTO(STATE_VALUE_SELECT)}};
fsm_state_event_t const value_save_events[] = {{EVENT_IMMEDIATE, ACTION_POP}};

fsm_state_entry_t const value_select_fsm[] = {
    {STATE_VALUE_INIT, nullptr, setupValueIndex, 0, value_init_events, ARRAY_SIZE(value_init_events)},
    {STATE_VALUE_SELECT, nullptr, displayValueIndex, 20000, value_select_events, ARRAY_SIZE(value_select_events)},
    {STATE_VALUE_INC, nullptr, incrementValueIndex, 0, value_increment_events, ARRAY_SIZE(value_increment_events)},
    {STATE_VALUE_DEC, nullptr, decrementValueIndex, 0, value_decrement_events, ARRAY_SIZE(value_decrement_events)},
    {STATE_VALUE_SAVE, nullptr, saveValueIndex, 0, value_save_events, ARRAY_SIZE(value_save_events)},
    {STATE_LAST}
};

fsm_state_event_t const value_menu_events[] = {MENU_EVENTS(value_select_fsm)};

// Power FSM
fsm_state_entry_t const power_menu_fsm[] = {
    {STATE_POWER_MAX, nullptr, displayMenuScreen, 20000, value_menu_events, ARRAY_SIZE(value_menu_events)},
    {STATE_POWER_DYNAMIC, nullptr, displayMenuScreen, 20000, value_menu_events, ARRAY_SIZE(value_menu_events)},
    {STATE_LAST}
};

// WiFi Update FSM
fsm_state_event_t const wifi_confirm_events[] = {
    {EVENT_TIMEOUT, ACTION_POPALL},
    {EVENT_LEFT, ACTION_POP},
    {EVENT_ENTER, GOTO(STATE_WIFI_EXECUTE)}
};
fsm_state_event_t const wifi_execute_events[] = {{EVENT_TIMEOUT, GOTO(STATE_WIFI_EXECUTE)}, {EVENT_LEFT, GOTO(STATE_WIFI_EXIT)}};
fsm_state_event_t const wifi_exit_events[] = {{EVENT_IMMEDIATE, ACTION_POP}};

fsm_state_entry_t const wifi_update_menu_fsm[] = {
    {STATE_WIFI_CONFIRM, nullptr, displayWiFiConfirm, 20000, wifi_confirm_events, ARRAY_SIZE(wifi_confirm_events)},
    {STATE_WIFI_EXECUTE, nullptr, executeWiFi, 1000, wifi_execute_events, ARRAY_SIZE(wifi_execute_events)},
    {STATE_WIFI_EXIT, nullptr, exitWiFi, 0, wifi_exit_events, ARRAY_SIZE(wifi_exit_events)},
    {STATE_LAST}
};
fsm_state_event_t const wifi_menu_update_events[] = {MENU_EVENTS(wifi_update_menu_fsm)};
fsm_state_event_t const wifi_ext_execute_events[] = {{EVENT_TIMEOUT, ACTION_POP}};
fsm_state_entry_t const wifi_ext_menu_fsm[] = {
    {STATE_WIFI_EXECUTE, nullptr, executeWiFi, 1000, wifi_ext_execute_events, ARRAY_SIZE(wifi_ext_execute_events)},
    {STATE_LAST}
};
fsm_state_event_t const wifi_ext_menu_events[] = {MENU_EVENTS(wifi_ext_menu_fsm)};
fsm_state_entry_t const wifi_menu_fsm[] = {
    {STATE_WIFI_TX, nullptr, displayMenuScreen, 20000, wifi_menu_update_events, ARRAY_SIZE(wifi_menu_update_events)},
    {STATE_WIFI_RX, nullptr, displayMenuScreen, 20000, wifi_ext_menu_events, ARRAY_SIZE(wifi_ext_menu_events)},
    {STATE_LAST}
};

// Bind FSM
fsm_state_event_t const bind_confirm_events[] = {
    {EVENT_TIMEOUT, ACTION_POPALL},
    {EVENT_LEFT, ACTION_POP},
    {EVENT_ENTER, GOTO(STATE_BIND_EXECUTE)}
};
fsm_state_event_t const bind_execute_events[] = {{EVENT_TIMEOUT, GOTO(STATE_BIND_EXECUTE)}};

fsm_state_entry_t const bind_menu_fsm[] = {
    {STATE_BIND_CONFIRM, nullptr, displayBindConfirm, 20000, bind_confirm_events, ARRAY_SIZE(bind_confirm_events)},
    {STATE_BIND_EXECUTE, nullptr, executeBind, 1000, bind_execute_events, ARRAY_SIZE(bind_execute_events)},
    {STATE_LAST}
};

// Main menu FSM
fsm_state_event_t const power_menu_events[] = {MENU_EVENTS(power_menu_fsm)};
fsm_state_event_t const bind_menu_events[] = {MENU_EVENTS(bind_menu_fsm)};
fsm_state_event_t const wifi_menu_events[] = {MENU_EVENTS(wifi_menu_fsm)};

fsm_state_entry_t const main_menu_fsm[] = {
    {STATE_PACKET, nullptr, displayMenuScreen, 20000, value_menu_events, ARRAY_SIZE(value_menu_events)},
    {STATE_SWITCH, nullptr, displayMenuScreen, 20000, value_menu_events, ARRAY_SIZE(value_menu_events)},
    {STATE_POWER, nullptr, displayMenuScreen, 20000, power_menu_events, ARRAY_SIZE(power_menu_events)},
    {STATE_TELEMETRY, nullptr, displayMenuScreen, 20000, value_menu_events, ARRAY_SIZE(value_menu_events)},
    {STATE_BIND, nullptr, displayMenuScreen, 20000, bind_menu_events, ARRAY_SIZE(bind_menu_events)},
    {STATE_WIFI, nullptr, displayMenuScreen, 20000, wifi_menu_events, ARRAY_SIZE(wifi_menu_events)},
    {STATE_LAST}
};

// Linkstats FSM
fsm_state_event_t const linkstats_confirm_events[] = {
    {EVENT_TIMEOUT, GOTO(STATE_LINKSTATS)},
    {EVENT_LONG_ENTER, PUSH(main_menu_fsm)},
    {EVENT_LONG_RIGHT, PUSH(main_menu_fsm)},
    {EVENT_UP, ACTION_POPALL},
    {EVENT_DOWN, ACTION_POPALL}
};
fsm_state_entry_t const linkstats_menu_fsm[] = {
    {STATE_LINKSTATS, nullptr, displayLinkstats, 1000, linkstats_confirm_events, ARRAY_SIZE(linkstats_confirm_events)},
    {STATE_LAST}
};

// Entry FSM
fsm_state_event_t const splash_events[] = {
    {EVENT_TIMEOUT, GOTO(STATE_IDLE)}
};
fsm_state_event_t const idle_events[] = {
    {EVENT_TIMEOUT, GOTO(STATE_IDLE)},
    {EVENT_LONG_ENTER, PUSH(main_menu_fsm)},
    {EVENT_LONG_RIGHT, PUSH(main_menu_fsm)},
    {EVENT_UP, PUSH(linkstats_menu_fsm)},
    {EVENT_DOWN, PUSH(linkstats_menu_fsm)}
};
fsm_state_entry_t const entry_fsm[] = {
    {STATE_SPLASH, nullptr, displaySplashScreen, 3000, splash_events, ARRAY_SIZE(splash_events)},
    {STATE_IDLE, nullptr, displayIdleScreen, 100, idle_events, ARRAY_SIZE(idle_events)},
    {STATE_LAST}
};

void jumpToWifiRunning()
{
    state_machine.jumpTo(wifi_menu_fsm, STATE_WIFI_TX);
    state_machine.jumpTo(wifi_update_menu_fsm, STATE_WIFI_EXECUTE);
}
