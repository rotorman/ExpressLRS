#ifndef H_CRSF
#define H_CRSF

#include "crsf_protocol.h"

class CRSF
{
public:
    static crsfLinkStatistics_t LinkStatistics; // Link Statistics Stored as Struct
    static void GetDeviceInformation(uint8_t *frame, uint8_t fieldCount);
    static uint32_t VersionStrToU32(const char *verStr);
};

extern GENERIC_CRC8 crsf_crc;

#endif