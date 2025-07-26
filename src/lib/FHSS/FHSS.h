#pragma once

#include "targets.h"
#include "random.h"

#if defined(RADIO_SX128X)
#define FreqCorrectionMax ((int32_t)(200000/FREQ_STEP))
#endif
#define FreqCorrectionMin (-FreqCorrectionMax)

#define FREQ_HZ_TO_REG_VAL(freq) ((uint32_t)((double)freq/(double)FREQ_STEP))
#define FREQ_SPREAD_SCALE 256
#define FHSS_SEQUENCE_LEN 256

typedef struct {
    const char  *domain;
    uint32_t    freq_start;
    uint32_t    freq_stop;
    uint32_t    freq_count;
    uint32_t    freq_center;
} fhss_config_t;

extern volatile uint8_t FHSSptr;

// Primary Band
extern uint16_t primaryBandCount;
extern uint32_t freq_spread;
extern uint8_t FHSSsequence[];
extern uint_fast8_t sync_channel;
extern const fhss_config_t *FHSSconfig;

// create and randomise an FHSS sequence
void FHSSrandomiseFHSSsequence(uint32_t seed);
void FHSSrandomiseFHSSsequenceBuild(uint32_t seed, uint32_t freqCount, uint_fast8_t sync_channel, uint8_t *sequence);

static inline uint32_t FHSSgetMinimumFreq(void)
{
    return FHSSconfig->freq_start;
}

static inline uint32_t FHSSgetMaximumFreq(void)
{
    return FHSSconfig->freq_stop;
}

// The number of frequencies for this regulatory domain
static inline uint32_t FHSSgetChannelCount(void)
{
    return FHSSconfig->freq_count;
}

// get the number of entries in the FHSS sequence
static inline uint16_t FHSSgetSequenceCount()
{
    return primaryBandCount;
}

// get the initial frequency, which is also the sync channel
static inline uint32_t FHSSgetInitialFreq()
{
    return FHSSconfig->freq_start + (sync_channel * freq_spread / FREQ_SPREAD_SCALE);
}

// Get the current sequence pointer
static inline uint8_t FHSSgetCurrIndex()
{
    return FHSSptr;
}

// Is the current frequency the sync frequency
static inline uint8_t FHSSonSyncChannel()
{
    return FHSSsequence[FHSSptr] == sync_channel;
}

// Set the sequence pointer, used by RX on SYNC
static inline void FHSSsetCurrIndex(const uint8_t value)
{
    FHSSptr = value % FHSSgetSequenceCount();
}

// Advance the pointer to the next hop and return the frequency of that channel
static inline uint32_t FHSSgetNextFreq()
{
    FHSSptr = (FHSSptr + 1) % FHSSgetSequenceCount();

    return FHSSconfig->freq_start + (freq_spread * FHSSsequence[FHSSptr] / FREQ_SPREAD_SCALE);
}

static inline const char *FHSSgetRegulatoryDomain()
{
    return FHSSconfig->domain;
}
