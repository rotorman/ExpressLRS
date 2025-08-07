#pragma once

#include "targets.h"
#include <device.h>

#define BINDINGTIMEOUTMS 5000

typedef enum
{
    RECEIVERCONNECTED,
    AWAITINGMODELIDFROMHANDSET,
    RECEIVERDISCONNECTED,
    MODE_STATES,
    // States below here are special mode states
    noHandsetCommunication,
    // Failure states go below here to display immediately
    FAILURE_STATES,
    radioFailed,
    hardwareUndefined
} connectionState_e;

// Limited to 16 possible ACTIONs by config storage currently
typedef enum : uint8_t {
    ACTION_NONE,
    ACTION_BIND,
    ACTION_RESET_REBOOT,
    ACTION_LAST
} action_e;

enum eAuxChannels : uint8_t
{
    AUX1 = 4,
    AUX2 = 5,
    AUX3 = 6,
    AUX4 = 7,
    AUX5 = 8,
    AUX6 = 9,
    AUX7 = 10,
    AUX8 = 11,
    AUX9 = 12,
    AUX10 = 13,
    AUX11 = 14,
    AUX12 = 15,
    CRSF_NUM_CHANNELS = 16
};

extern bool connectionHasModelMatch;
extern bool InBindingMode;
extern volatile uint16_t ChannelData[CRSF_NUM_CHANNELS]; // Current state of channels, CRSF format

extern connectionState_e connectionState;
inline void setConnectionState(connectionState_e newState) {
    connectionState = newState;
    devicesTriggerEvent(EVENT_CONNECTION_CHANGED);
}

extern void EnterBindingMode(); // defined in rx_main/tx_main
