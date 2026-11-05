// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J touch bus and power. The Goodix GT9916R ("goodix,brl-d") sits on the QUP1 SE4 pins (GPIO 48 MISO, 49 MOSI,
// 50 CLK, 51 CS), with reset GPIO 161 (high runs), interrupt GPIO 162 (falling edge), IO supply switch GPIO 28 and
// analog supply L14B (pm_humu_l14, 3.2 V, RPMh resource "ldob14"). Values come from the stock dtbo (MTP overlay,
// fragment@73) and the vendor_boot device tree.
//
// SE4 is loaded as SPI (fw 0x102) with its FIFO interface disabled, the GSI-only setup houji also found on SM8650, so
// the pins run as GPIOs and SPI mode 0 is clocked by hand. L14B is voted on through the apps RSC (DRV2, one idle active
// TCS, voltage/mode/enable with completion) after checking the command DB entry. Both were verified on the device:
// rpmh "ok", Berlin handshake ok, version block "BERLIN" / patch 9916R.
#include "GoodixBus.h"

#include <stdarg.h>
#include <string.h>
#include <IOKit/IOLib.h>

extern "C" {
vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
}

namespace nx769j_touch {
namespace {
constexpr uint64_t kTlmm = 0xf100000, kTlmmSize = 0xb0000;
constexpr uint64_t kCmdDb = 0x81c60000, kCmdDbSize = 0x20000;   // reserved-memory aop_cmd_db_region
constexpr uint64_t kRscDrv2 = 0x17a20000, kRscSize = 0x10000;   // apps_rsc drv-2 (HLOS)
constexpr uint32_t kTcsOffset = 0xd00;                          // qcom,tcs-offset
constexpr unsigned kActiveTcs = 3;                              // tcs-config: ACTIVE 3 first, so TCS 0..2
constexpr unsigned kMiso = 48, kMosi = 49, kClk = 50, kCs = 51, kIoSupply = 28, kReset = 161, kIrq = 162;
constexpr unsigned kLdoMillivolts = 3200, kLdoModeHpm = 7, kVrmSlave = 4;   // cmd-db.c: ARC 3, VRM 4, BCM 5

volatile uint32_t *gTlmm;

volatile uint32_t *map(uint64_t pa, size_t size) { return reinterpret_cast<volatile uint32_t *>(ml_io_map(pa, size)); }
uint32_t rd(volatile uint32_t *base, uint32_t off) { return base[off / 4]; }
void wr(volatile uint32_t *base, uint32_t off, uint32_t v) { base[off / 4] = v; __builtin_arm_dsb(0xf); }

// TLMM per pin (pinctrl-sm8650.c): CFG +0 (pull 1:0, function 5:2, drive 8:6, output enable 9), IN_OUT +4 (bit 0 in,
// bit 1 out), INTR_CFG +8 (enable 0, polarity 1, detection 3:2, raw status 4), INTR_STATUS +0xc (bit 0, write 0 acks).
void set_out(unsigned n, int high) { wr(gTlmm, n * 0x1000 + 4, high ? 2 : 0); }
int get_in(unsigned n) { return rd(gTlmm, n * 0x1000 + 4) & 1; }
void make_output(unsigned n, int high)
{
	set_out(n, high);
	wr(gTlmm, n * 0x1000, (2u << 6) | (1u << 9));                    // GPIO, no pull, 6 mA, output
}

// SPI mode 0 by hand: MOSI changes while CLK is low, MISO is sampled after the rising edge.
uint8_t xfer_byte(uint8_t out)
{
	uint8_t in = 0;
	for (int bit = 7; bit >= 0; bit--) {
		set_out(kMosi, (out >> bit) & 1);
		set_out(kClk, 1);
		in = (uint8_t)((in << 1) | get_in(kMiso));
		set_out(kClk, 0);
	}
	return in;
}

// Command DB (cmd-db.c layout): version, magic 0x0c0330db, 8 slave headers of 16 bytes, checksum, reserved, then data at
// 144. Entry headers are 24 bytes: id[8], priority[2], addr, len, offset. Returns the ldob14 address or 0.
uint32_t cmd_db_ldob14()
{
	const volatile uint8_t *db = reinterpret_cast<const volatile uint8_t *>(map(kCmdDb, kCmdDbSize));
	static uint8_t copy[kCmdDbSize];
	for (size_t i = 0; i < sizeof(copy); i += 4) {
		uint32_t w = *reinterpret_cast<const volatile uint32_t *>(db + i);
		memcpy(copy + i, &w, 4);
	}
	if (le32(copy + 4) != 0x0c0330db) {
		line("cmddb: bad magic 0x%08x", le32(copy + 4));
		return 0;
	}
	uint32_t found = 0, hits = 0, slave = 0;
	for (unsigned h = 0; h < 8; h++) {
		const uint8_t *r = copy + 8 + 16 * h;
		unsigned id = le16(r), count = le16(r + 6);
		if (!id) {
			break;
		}
		size_t start = 144u + le16(r + 2);
		if (start + (size_t)count * 24 > sizeof(copy)) {
			line("cmddb: slave %u table out of range", id);
			return 0;
		}
		for (unsigned e = 0; e < count; e++) {
			const uint8_t *ent = copy + start + 24 * e;
			if (!memcmp(ent, "ldob14\0\0", 8)) {
				found = le32(ent + 16);
				slave = id;
				hits++;
			}
		}
	}
	if (hits != 1 || slave != kVrmSlave || ((found >> 16) & 7) != kVrmSlave || (found & 3)) {
		line("cmddb: ldob14 0x%05x slave %u hits %u not trusted, no vote", found, slave, hits);
		return 0;
	}
	return found;
}

// apps RSC DRV2 register layout (rpmh-rsc.c): v3 moved the per-TCS registers.
struct RscRegs {
	uint32_t tcs_stride, cmd_stride, irq_en, irq_stat, irq_clr, control, status, cmd_enable, msgid, addr, data, cmd_status;
};
constexpr RscRegs kRscV2 = {672, 20, 0x00, 0x04, 0x08, 0x14, 0x18, 0x1c, 0x30, 0x34, 0x38, 0x3c};
constexpr RscRegs kRscV3 = {672, 24, 0x00, 0x04, 0x08, 0x24, 0x28, 0x2c, 0x34, 0x38, 0x3c, 0x40};

bool rpmh_vote_ldo(uint32_t res)
{
	volatile uint32_t *drv = map(kRscDrv2, kRscSize);
	unsigned major = (rd(drv, 0) >> 16) & 0xff;
	const RscRegs &r = major == 3 ? kRscV3 : kRscV2;
	auto tcs = [&](unsigned t, uint32_t reg) { return kTcsOffset + r.tcs_stride * t + reg; };
	auto cmd = [&](unsigned t, unsigned c, uint32_t reg) { return tcs(t, reg) + r.cmd_stride * c; };

	int pick = -1;
	for (unsigned t = 0; t < kActiveTcs && pick < 0; t++) {
		if (rd(drv, tcs(t, r.status)) && !rd(drv, tcs(t, r.cmd_enable))) {
			pick = (int)t;
		}
	}
	if (pick < 0) {
		line("rsc: no idle active TCS, no vote");
		return false;
	}
	const unsigned t = (unsigned)pick;
	const uint32_t msgid = 8u | (1u << 16) | (1u << 8);            // 8 bytes, write, response required
	const uint32_t addrs[3] = {res + 0x0, res + 0x8, res + 0x4};    // voltage (mV), mode, enable: the Linux order
	const uint32_t datas[3] = {kLdoMillivolts, kLdoModeHpm, 1};
	for (unsigned c = 0; c < 3; c++) {
		wr(drv, cmd(t, c, r.msgid), msgid);
		wr(drv, cmd(t, c, r.addr), addrs[c]);
		wr(drv, cmd(t, c, r.data), datas[c]);
	}
	wr(drv, tcs(t, r.cmd_enable), 0x7);
	wr(drv, kTcsOffset + r.irq_en, rd(drv, kTcsOffset + r.irq_en) | (1u << t));
	// __tcs_set_trigger: clear trigger, clear enable, set AMC enable, then trigger.
	uint32_t ctl = rd(drv, tcs(t, r.control)) & ~(1u << 24);
	wr(drv, tcs(t, r.control), ctl);
	ctl &= ~(1u << 16);
	wr(drv, tcs(t, r.control), ctl);
	wr(drv, tcs(t, r.control), 1u << 16);
	wr(drv, tcs(t, r.control), (1u << 16) | (1u << 24));

	unsigned waited = 0;
	while (!(rd(drv, kTcsOffset + r.irq_stat) & (1u << t)) && waited < 100000) {
		IODelay(10);
		waited += 10;
	}
	uint32_t stat = rd(drv, kTcsOffset + r.irq_stat);
	uint32_t s0 = rd(drv, cmd(t, 0, r.cmd_status)), s1 = rd(drv, cmd(t, 1, r.cmd_status)), s2 = rd(drv, cmd(t, 2, r.cmd_status));
	wr(drv, tcs(t, r.cmd_enable), 0);
	wr(drv, kTcsOffset + r.irq_clr, 1u << t);
	const uint32_t done = (1u << 8) | (1u << 16);                   // issued, completed
	bool ok = (stat & (1u << t)) && (s0 & done) == done && (s1 & done) == done && (s2 & done) == done;
	line("rpmh: L14B tcs %u %u us cmd %x %x %x -> %s", t, waited, s0, s1, s2, ok ? "ok" : "FAIL");
	return ok;
}
} // namespace

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

void bus_init()
{
	gTlmm = map(kTlmm, kTlmmSize);
	wr(gTlmm, kMiso * 0x1000, (2u << 6) | 1u);                        // MISO: input, pull-down
	make_output(kMosi, 0);
	make_output(kClk, 0);
	make_output(kCs, 1);
	make_output(kReset, 0);
	make_output(kIoSupply, 0);
	wr(gTlmm, kIrq * 0x1000, (2u << 6) | 3u);                         // interrupt pin: input, pull-up
	// Falling edge (detection 2, polarity 1) latched in INTR_STATUS by the raw status enable; not enabled to the GIC.
	wr(gTlmm, kIrq * 0x1000 + 8, (1u << 4) | (2u << 2) | (1u << 1));
	wr(gTlmm, kIrq * 0x1000 + 0xc, 0);
}

void chip_power(bool on)
{
	if (!on) {
		set_out(kReset, 0);
		set_out(kIoSupply, 0);
		return;
	}
	set_out(kIoSupply, 1);
	IOSleep(3 + 15);                                                // vendor: 3 ms after IOVDD, 15 ms after AVDD
	set_out(kReset, 1);
	IOSleep(4);
}

void chip_reset_pulse()
{
	set_out(kReset, 0);
	IOSleep(2);
	set_out(kReset, 1);
	IOSleep(100);
}

bool avdd_on()
{
	uint32_t res = cmd_db_ldob14();
	return res && rpmh_vote_ldo(res);
}

void spi_read(uint32_t addr, uint8_t *buf, size_t len)
{
	const uint8_t head[8] = {0xf1, (uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr,
	    0xff, 0xff, 0xff};
	set_out(kCs, 0);
	for (uint8_t b : head) {
		xfer_byte(b);
	}
	for (size_t i = 0; i < len; i++) {
		buf[i] = xfer_byte(0xff);
	}
	set_out(kCs, 1);
	IODelay(10);
}

void spi_write(uint32_t addr, const uint8_t *buf, size_t len)
{
	const uint8_t head[5] = {0xf0, (uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
	set_out(kCs, 0);
	for (uint8_t b : head) {
		xfer_byte(b);
	}
	for (size_t i = 0; i < len; i++) {
		xfer_byte(buf[i]);
	}
	set_out(kCs, 1);
	IODelay(10);
}

bool irq_pending()
{
	bool latched = rd(gTlmm, kIrq * 0x1000 + 0xc) & 1;
	if (latched) {
		wr(gTlmm, kIrq * 0x1000 + 0xc, 0);
	}
	return latched || !get_in(kIrq);
}

int irq_level() { return get_in(kIrq); }

} // namespace nx769j_touch
