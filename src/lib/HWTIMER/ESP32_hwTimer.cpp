#include "hwTimer.h"

void (*hwTimer::callbackTick)() = nullptr;
void (*hwTimer::callbackTock)() = nullptr;

volatile bool hwTimer::running = false;
volatile bool hwTimer::isTick = false;

volatile uint32_t hwTimer::HWtimerInterval = TimerIntervalUSDefault;
volatile int32_t hwTimer::PhaseShift = 0;
volatile int32_t hwTimer::FreqOffset = 0;

// Internal implementation specific variables
static hw_timer_t *timer = NULL;
static portMUX_TYPE isrMutex = portMUX_INITIALIZER_UNLOCKED;

#define HWTIMER_TICKS_PER_US 1

void ICACHE_RAM_ATTR hwTimer::init(void (*callbackTick)(), void (*callbackTock)())
{

    if (!timer)
    {
        hwTimer::callbackTick = callbackTick;
        hwTimer::callbackTock = callbackTock;
        timer = timerBegin(0, (APB_CLK_FREQ / 1000000 / HWTIMER_TICKS_PER_US), true);
        timerAttachInterrupt(timer, hwTimer::callback, true);
    }
}

void ICACHE_RAM_ATTR hwTimer::stop()
{
    if (timer && running)
    {
        running = false;
        timerAlarmDisable(timer);
    }
}

void ICACHE_RAM_ATTR hwTimer::resume()
{
    if (timer && !running)
    {
        // The timer must be restarted so that the new timerAlarmWrite() period is set.
        timerRestart(timer);
        timerAlarmWrite(timer, HWtimerInterval, true);
        running = true;
        timerAlarmEnable(timer);
    }
}

void ICACHE_RAM_ATTR hwTimer::updateInterval(uint32_t time)
{
    // timer should not be running when updateInterval() is called
    HWtimerInterval = time * HWTIMER_TICKS_PER_US;
    if (timer)
    {
        timerAlarmWrite(timer, HWtimerInterval, true);
    }
}

void ICACHE_RAM_ATTR hwTimer::phaseShift(int32_t newPhaseShift)
{
    int32_t minVal = -(HWtimerInterval >> 2);
    int32_t maxVal = (HWtimerInterval >> 2);

    // phase shift is in microseconds
    PhaseShift = constrain(newPhaseShift, minVal, maxVal) * HWTIMER_TICKS_PER_US;
}

void ICACHE_RAM_ATTR hwTimer::callback(void)
{
    if (running)
    {
        portENTER_CRITICAL_ISR(&isrMutex);
        callbackTock();
        portEXIT_CRITICAL_ISR(&isrMutex);
    }
}
