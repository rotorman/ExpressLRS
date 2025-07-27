#include "targets.h"
#include "CRSF.h"
#include "CRSFHandset.h"
#include "devHandset.h"

Handset *handset;

static bool initialize()
{
    handset = new CRSFHandset();
    return true;
}

static int start()
{
    handset->Begin();
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
