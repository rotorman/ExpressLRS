#include "SPIEx.h"

#include <soc/spi_struct.h>

void ICACHE_RAM_ATTR SPIExClass::_transfer(uint8_t cs_mask, uint8_t *data, uint32_t size, bool reading)
{
    spi_dev_t *spi = *(reinterpret_cast<spi_dev_t**>(bus()));
    // wait for SPI to become non-busy
    while(spi->cmd.usr) {}

    // Set the CS pins which we want controlled by the SPI module for this operation
    spiDisableSSPins(bus(), ~cs_mask);
    spiEnableSSPins(bus(), cs_mask);

#if defined(PLATFORM_ESP32_S3) || defined(PLATFORM_ESP32_C3)
    spi->ms_dlen.ms_data_bitlen = (size*8)-1;
#else
    spi->mosi_dlen.usr_mosi_dbitlen = ((size * 8) - 1);
    spi->miso_dlen.usr_miso_dbitlen = ((size * 8) - 1);
#endif

    // write the data to the SPI FIFO
    const uint32_t words = (size + 3) / 4;
    auto * const wordsBuf = reinterpret_cast<uint32_t *>(data);
    for(int i=0; i<words; i++)
    {
        spi->data_buf[i] = wordsBuf[i]; //copy buffer to spi fifo
    }

#if defined(PLATFORM_ESP32_S3) || defined(PLATFORM_ESP32_C3)
    spi->cmd.update = 1;
    while (spi->cmd.update) {}
#endif
    // start the SPI module
    spi->cmd.usr = 1;

    if (reading)
    {
        // wait for SPI write to complete
        while(spi->cmd.usr) {}

        for(int i=0; i<words; i++)
        {
            wordsBuf[i] = spi->data_buf[i]; //copy spi fifo to buffer
        }
    }
}

#if defined(PLATFORM_ESP32_S3) || defined(PLATFORM_ESP32_C3)
SPIExClass SPIEx(FSPI);
#else
SPIExClass SPIEx(VSPI);
#endif
