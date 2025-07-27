#include "targets.h"
#include "CRSF.h"
#include "CRSFHandset.h"
#include "devHandset.h"
#include "AutoDetect.h"

Handset *handset;

static bool initialize()
{
    if (GPIO_PIN_RCSIGNAL_RX == GPIO_PIN_RCSIGNAL_TX)
    {
        handset = new AutoDetect();
        return true;
    }
    handset = new CRSFHandset();
    return true;
}

static int start()
{
    handset->Begin();
#if defined(DEBUG_TX_FREERUN)
    handset->forceConnection();
#endif
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    handset->handleInput();
    return DURATION_IMMEDIATELY;
}

static int event()
{
    return DURATION_IGNORE;
}

device_t Handset_device = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_POWER_CHANGED
};
