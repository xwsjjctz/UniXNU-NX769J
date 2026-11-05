// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J touch bus and power: GPIO SPI to the Goodix GT9916R and the L14B RPMh vote. See GoodixBus.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nx769j_touch {

// Status lines kept for the heartbeat (GoodixTouch.cpp prints them).
void line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Maps TLMM and puts the touch pins in their GPIO roles: SPI lines, reset held low, IO supply off, interrupt pin
// input with pull-up and its falling-edge latch armed (raw status only, not routed to the GIC).
void bus_init();
// IO supply on, wait for the rails, release reset. Off holds reset and drops the IO supply.
void chip_power(bool on);
void chip_reset_pulse();
// Votes L14B (3.2 V, HPM, enabled) through the apps RSC after validating the command DB entry. Logs each step.
bool avdd_on();

// Berlin SPI: read is F1 + 32-bit big-endian address + 3 dummy bytes, write is F0 + address.
void spi_read(uint32_t addr, uint8_t *buf, size_t len);
void spi_write(uint32_t addr, const uint8_t *buf, size_t len);

// Interrupt pin: the latched falling edge (cleared by the read) or a low level.
bool irq_pending();
int irq_level();

uint16_t le16(const uint8_t *p);
uint32_t le32(const uint8_t *p);

} // namespace nx769j_touch
