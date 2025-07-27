#include "common.h"

expresslrs_mod_settings_s ExpressLRS_AirRateConfig[RATE_MAX] = {
    {0, RADIO_TYPE_SX128x_FLRC, RATE_FLRC_2G4_1000HZ,     0x86, 0x10, 0x00, 32, TLM_RATIO_1_128, 2,  1000, 8U, 1},
    {1, RADIO_TYPE_SX128x_FLRC, RATE_FLRC_2G4_500HZ,      0x86, 0x10, 0x00, 32, TLM_RATIO_1_128, 2,  2000, 8U, 1},
    {2, RADIO_TYPE_SX128x_FLRC, RATE_FLRC_2G4_500HZ_DVDA, 0x86, 0x10, 0x00, 32, TLM_RATIO_1_128, 2,  1000, 8U, 2},
    {3, RADIO_TYPE_SX128x_FLRC, RATE_FLRC_2G4_250HZ_DVDA, 0x86, 0x10, 0x00, 32, TLM_RATIO_1_128, 2,  1000, 8U, 4},
    {4, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_500HZ,      0x18, 0x50, 0x06, 12, TLM_RATIO_1_128, 4,  2000, 8U, 1},
    {5, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_333HZ_8CH,  0x18, 0x50, 0x07, 12, TLM_RATIO_1_128, 4,  3003, 13U, 1},
    {6, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_250HZ,      0x18, 0x60, 0x07, 14, TLM_RATIO_1_64,  4,  4000, 8U, 1},
    {7, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_150HZ,      0x18, 0x70, 0x07, 12, TLM_RATIO_1_32,  4,  6666, 8U, 1},
    {8, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_100HZ_8CH,  0x18, 0x70, 0x07, 12, TLM_RATIO_1_32,  4, 10000, 13U, 1},
    {9, RADIO_TYPE_SX128x_LORA, RATE_LORA_2G4_50HZ,       0x18, 0x80, 0x07, 12, TLM_RATIO_1_16,  2, 20000, 8U, 1}};

expresslrs_rf_pref_params_s ExpressLRS_AirRateRFperf[RATE_MAX] = {
    {0, -104,   389, 2500, 2500,  3, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {1, -104,   389, 2500, 2500,  3, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {2, -104,   389, 2500, 2500,  3, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {3, -104,   389, 2500, 2500,  3, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {4, -105,  1507, 2500, 2500,  3, 5000, 20, 38},
    {5, -105,  2374, 2500, 2500,  4, 5000, 20, 38},
    {6, -108,  3300, 3000, 2500,  6, 5000, 12, 38},
    {7, -112,  5871, 3500, 2500, 10, 5000, 0, 34},
    {8, -112,  7605, 3500, 2500, 11, 5000, 0, 34},
    {9, -115, 10798, 4000, 2500,  0, 5000, -1, 26}};

expresslrs_mod_settings_s *get_elrs_airRateConfig(uint8_t index)
{
    if (RATE_MAX <= index)
    {
        // Set to last usable entry in the array
        index = RATE_MAX - 1;
    }
    return &ExpressLRS_AirRateConfig[index];
}

expresslrs_rf_pref_params_s *get_elrs_RFperfParams(uint8_t index)
{
    if (RATE_MAX <= index)
    {
        // Set to last usable entry in the array
        index = RATE_MAX - 1;
    }
    return &ExpressLRS_AirRateRFperf[index];
}

uint8_t ICACHE_RAM_ATTR enumRatetoIndex(expresslrs_RFrates_e const eRate)
{ // convert enum_rate to index
    expresslrs_mod_settings_s const * ModParams;
    for (uint8_t i = 0; i < RATE_MAX; i++)
    {
        ModParams = get_elrs_airRateConfig(i);
        if (ModParams->enum_rate == eRate)
        {
            return i;
        }
    }
    // If 25Hz selected and not available, return the slowest rate available
    // else return the fastest rate available (500Hz selected but not available)
    return (eRate == RATE_LORA_900_25HZ) ? RATE_MAX - 1 : 0;
}

// Connection state information
uint8_t UID[UID_LEN] = {0};  // "bind phrase" ID
bool connectionHasModelMatch = false;
bool InBindingMode = false;
uint8_t ExpressLRS_currTlmDenom = 1;
connectionState_e connectionState = disconnected;

// Current state of channels, CRSF format
uint32_t ChannelData[CRSF_NUM_CHANNELS];

uint8_t ICACHE_RAM_ATTR TLMratioEnumToValue(expresslrs_tlm_ratio_e const enumval)
{
    // !! TLM_RATIO_STD/TLM_RATIO_DISARMED should be converted by the caller !!
    if (enumval == TLM_RATIO_NO_TLM)
        return 1;

    // 1 << (8 - (enumval - TLM_RATIO_NO_TLM))
    // 1_128 = 128, 1_64 = 64, 1_32 = 32, etc
    return 1 << (8 + TLM_RATIO_NO_TLM - enumval);
}

/***
 * @brief: Calculate number of 'burst' telemetry frames for the specified air rate and tlm ratio
 *
 * When attempting to send a LinkStats telemetry frame at most every TELEM_MIN_LINK_INTERVAL_MS,
 * calculate the number of sequential advanced telemetry frames before another LinkStats is due.
 ****/
uint8_t TLMBurstMaxForRateRatio(uint16_t const rateHz, uint8_t const ratioDiv)
{
    // Maximum ms between LINK_STATISTICS packets for determining burst max
    constexpr uint32_t TELEM_MIN_LINK_INTERVAL_MS = 512U;

    // telemInterval = 1000 / (hz / ratiodiv);
    // burst = TELEM_MIN_LINK_INTERVAL_MS / telemInterval;
    // This ^^^ rearranged to preserve precision vvv, using u32 because F1000 1:2 = 256
    unsigned retVal = TELEM_MIN_LINK_INTERVAL_MS * rateHz / ratioDiv / 1000U;

    // Reserve one slot for LINK telemetry. 256 becomes 255 here, safe for return in uint8_t
    if (retVal > 1)
        --retVal;
    else
        retVal = 1;

    return retVal;
}

uint32_t uidMacSeedGet()
{
    const uint32_t macSeed = ((uint32_t)UID[2] << 24) + ((uint32_t)UID[3] << 16) +
                             ((uint32_t)UID[4] << 8) + (UID[5]^OTA_VERSION_ID);
    return macSeed;
}
