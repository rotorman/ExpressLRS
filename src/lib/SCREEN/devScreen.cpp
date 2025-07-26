#include "targets.h"
#include "devScreen.h"

#include "common.h"

#include "OLED/oleddisplay.h"
#include "TFT/tftdisplay.h"

#include "devButton.h"
#include "handset.h"

FiniteStateMachine state_machine(entry_fsm);

Display *display;

#include "FiveWayButton/FiveWayButton.h"
FiveWayButton fivewaybutton;

#define SCREEN_DURATION 20

extern void jumpToWifiRunning();

static int handle(void)
{
    uint32_t now = millis();

    if (state_machine.getParentState() != STATE_WIFI_TX && connectionState == wifiUpdate)
    {
        jumpToWifiRunning();
    }
    
    if (!handset->IsArmed())
    {
        int key;
        bool isLongPressed;
        // if we are using analog joystick then we can't cancel because WiFi is using the ADC2 (i.e. channel >= 8)!
        if (connectionState == wifiUpdate && digitalPinToAnalogChannel(GPIO_PIN_JOYSTICK) >= 8)
        {
            key = INPUT_KEY_NO_PRESS;
        }
        else
        {
            fivewaybutton.update(&key, &isLongPressed);
        }
        fsm_event_t fsm_event;
        switch (key)
        {
        case INPUT_KEY_DOWN_PRESS:
            fsm_event = EVENT_DOWN;
            break;
        case INPUT_KEY_UP_PRESS:
            fsm_event = EVENT_UP;
            break;
        case INPUT_KEY_LEFT_PRESS:
            fsm_event = EVENT_LEFT;
            break;
        case INPUT_KEY_RIGHT_PRESS:
            fsm_event = EVENT_RIGHT;
            break;
        case INPUT_KEY_OK_PRESS:
            fsm_event = EVENT_ENTER;
            break;
        default: // INPUT_KEY_NO_PRESS
            fsm_event = EVENT_TIMEOUT;
        }
        if (fsm_event != EVENT_TIMEOUT && isLongPressed)
        {
            fsm_event = (fsm_event | LONG_PRESSED);
        }
        state_machine.handleEvent(now, fsm_event);
    }
    else
    {
        state_machine.handleEvent(now, EVENT_TIMEOUT);
    }

    return SCREEN_DURATION;
}

static bool initialize()
{
    if (OPT_HAS_SCREEN)
    {
        fivewaybutton.init();
        if (OPT_HAS_TFT_SCREEN)
        {
            display = new TFTDisplay();
        }
        else
        {
            display = new OLEDDisplay();
        }
        display->init();
        state_machine.start(millis(), getInitialState());
    }
    return OPT_HAS_SCREEN;
}

static int start()
{
    return DURATION_IMMEDIATELY;
}

static int event()
{
    return handle();
}

static int timeout()
{
    return handle();
}

device_t Screen_device = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_CONNECTION_CHANGED | EVENT_ARM_FLAG_CHANGED
};
