#pragma once
// Stand-in for the core's SPI functions: keeps the last frame written, for the mapping test.
#include <cstdint>
#include <vector>

struct spi_t {};
#define VSPI 3
#define SPI_MODE0 0
#define SPI_MSBFIRST 1
extern std::vector<uint8_t> g_spiOut;
inline spi_t *spiStartBus(uint8_t, uint32_t, uint8_t, uint8_t) {
  static spi_t s;
  return &s;
}
inline uint32_t spiFrequencyToClockDiv(spi_t *, uint32_t) { return 0; }
inline void spiSetClockDiv(spi_t *, uint32_t) {}
inline bool spiAttachMOSI(spi_t *, int8_t) { return true; }
inline void spiWriteNL(spi_t *, const void *d, uint32_t n) {
  g_spiOut.assign((const uint8_t *)d, (const uint8_t *)d + n);
}

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
