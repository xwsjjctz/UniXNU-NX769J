// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J CPU frequency: a load-following governor on the four SM8650 EPSS domains (cpufreq-epss at 0x17d91000 +
// domain * 0x1000). Boot-arg nx769j-dvfs: 1 governor (default), 2 hold every domain at its highest non-boost row,
// 0 leave the boot chain's request and only report.
//
// Domains (qcom,freq-domain of each cpu node): 0 = cpu0-1 (A520), 3 = cpu2-4 (A720), 1 = cpu5-6 (A720), 2 = cpu7 (X4).
// Per domain (qcom-cpufreq-hw, EPSS): +0x0 enable (bit 0), +0x20 domain state (L_VAL in bits 7:0), +0xb0 control (bit 0
// set: one perf-state request per core), +0x100 LUT (40 rows: source 31:30, core count 18:16, L_VAL 7:0; freq = L_VAL
// x 19.2 MHz; a repeated row ends it; core count 1 marks a boost row), +0x320 perf-state request. EPSS picks the
// voltage for each row itself: nothing here touches a voltage, a PLL or the LUT.
//
// The governor samples each core's idle ticks every 50 ms, asks for 1.25 x the busy share of the current frequency,
// raises at once, lowers after four low samples, and keeps 75 % of the cap for a second after a touch.
#include <stdint.h>
#include <stddef.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

struct processor;
extern "C" {
vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
struct processor *cpu_to_processor(int cpu);
void processor_cpu_load_info(struct processor *processor, unsigned int ticks[4]);
uint64_t mach_absolute_time(void);
void absolutetime_to_nanoseconds(uint64_t abstime, uint64_t *result);
kern_return_t kernel_thread_start(thread_continue_t continuation, void *parameter, thread_t *new_thread);
void thread_deallocate(thread_t thread);
void nx769j_dvfs_start(void);
void nx769j_dvfs_touch(void);
void nx769j_dvfs_report(void);
}

namespace {
constexpr uint64_t kEpss = 0x17d91000, kQfprom = 0x221c2000;
constexpr unsigned kDomains = 4, kRows = 40, kCpus = 8;
constexpr unsigned kCpuDomain[kCpus] = {0, 0, 3, 3, 3, 1, 1, 2};
constexpr unsigned kDomainCores[kDomains] = {2, 2, 1, 3};
constexpr const char *kDomainCpus[kDomains] = {"0-1", "5-6", "7", "2-4"};
constexpr uint32_t kXoKhz = 19200, kMaxKhz = 3500000;
constexpr unsigned kSampleMs = 50, kLowerAfter = 4;
constexpr uint64_t kTouchBoostNs = 1000000000ull;
enum : unsigned { kTicksUser = 0, kTicksSystem = 1, kTicksIdle = 2 };

struct Domain {
	volatile uint32_t *regs;
	bool usable;
	unsigned rows, cur, lows, requests;
	uint8_t index[kRows];
	uint32_t khz[kRows];
	uint32_t boostKhz, initialIndex, busy;
	const char *why;
};

Domain gDomain[kDomains];
unsigned gMode;
bool gRunning;
volatile uint64_t gTouchAt;
uint32_t gSpeedBinByte = 0xffffffff;

uint32_t rd(volatile uint32_t *base, uint32_t off) { return base[off / 4]; }

uint32_t row_khz(uint32_t raw) { return (raw >> 30) ? (raw & 0xff) * kXoKhz : 0; }

// Reads the factory LUT. A domain is usable only when it is enabled and its rows rise monotonically up to the end marker.
void scan(unsigned id)
{
	Domain &d = gDomain[id];
	d.regs = reinterpret_cast<volatile uint32_t *>(ml_io_map(kEpss + id * 0x1000ull, 0x1000));
	d.why = "ok";
	if (!(rd(d.regs, 0) & 1)) {
		d.why = "disabled";
		return;
	}
	uint32_t prev = 0;
	for (unsigned i = 0; i < kRows; i++) {
		uint32_t raw = rd(d.regs, 0x100 + i * 4), khz = row_khz(raw), cores = (raw >> 16) & 7;
		if (!khz) {
			continue;                                               // alternate-clock row: not a perf state to ask for
		}
		if (i && khz == prev) {
			break;
		}
		if (khz < prev || khz > kMaxKhz) {
			d.why = "lut not monotonic";
			d.rows = 0;
			return;
		}
		prev = khz;
		if (cores == 1) {
			d.boostKhz = khz;
			continue;
		}
		d.khz[d.rows] = khz;
		d.index[d.rows++] = (uint8_t)i;
	}
	d.initialIndex = rd(d.regs, 0x320);
	d.cur = d.rows ? d.rows - 1 : 0;
	for (unsigned r = 0; r < d.rows; r++) {
		if (d.index[r] == d.initialIndex) {
			d.cur = r;
		}
	}
	d.usable = d.rows >= 2;
	if (!d.usable) {
		d.why = "too few rows";
	}
}

bool request(unsigned id, unsigned row)
{
	Domain &d = gDomain[id];
	if (!d.usable || row >= d.rows) {
		return false;
	}
	unsigned regs = (rd(d.regs, 0xb0) & 1) ? kDomainCores[id] : 1;
	for (unsigned c = 0; c < regs; c++) {
		d.regs[(0x320 + c * 4) / 4] = d.index[row];
	}
	__builtin_arm_dsb(0xf);
	for (unsigned c = 0; c < regs; c++) {
		if (rd(d.regs, 0x320 + c * 4) != d.index[row]) {
			d.usable = false;
			d.why = "request readback mismatch";
			return false;
		}
	}
	d.cur = row;
	d.requests++;
	return true;
}

unsigned row_for(const Domain &d, uint64_t khz)
{
	for (unsigned r = 0; r < d.rows; r++) {
		if (d.khz[r] >= khz) {
			return r;
		}
	}
	return d.rows - 1;
}

void governor(void *, wait_result_t)
{
	unsigned prev[kCpus][4] = {};
	for (unsigned c = 0; c < kCpus; c++) {
		processor_cpu_load_info(cpu_to_processor((int)c), prev[c]);
	}
	for (;;) {
		IOSleep(kSampleMs);
		unsigned busy[kDomains] = {};
		for (unsigned c = 0; c < kCpus; c++) {
			unsigned now[4] = {};
			processor_cpu_load_info(cpu_to_processor((int)c), now);
			unsigned work = (now[kTicksUser] - prev[c][kTicksUser]) + (now[kTicksSystem] - prev[c][kTicksSystem]);
			unsigned idle = now[kTicksIdle] - prev[c][kTicksIdle];
			for (unsigned s = 0; s < 4; s++) {
				prev[c][s] = now[s];
			}
			unsigned share = (work + idle) ? work * 100 / (work + idle) : 0;
			unsigned &b = busy[kCpuDomain[c]];
			b = share > b ? share : b;
		}
		uint64_t sinceTouch = 0;
		absolutetime_to_nanoseconds(mach_absolute_time() - gTouchAt, &sinceTouch);
		bool touched = gTouchAt && sinceTouch < kTouchBoostNs;
		for (unsigned id = 0; id < kDomains; id++) {
			Domain &d = gDomain[id];
			if (!d.usable) {
				continue;
			}
			d.busy = busy[id];
			uint64_t want = (uint64_t)d.khz[d.cur] * busy[id] * 125 / 10000;
			uint64_t floor = touched ? (uint64_t)d.khz[d.rows - 1] * 3 / 4 : 0;
			unsigned row = row_for(d, want > floor ? want : floor);
			if (row > d.cur) {
				d.lows = 0;
				request(id, row);
			} else if (row < d.cur) {
				if (++d.lows >= kLowerAfter) {
					d.lows = 0;
					request(id, row);
				}
			} else {
				d.lows = 0;
			}
		}
	}
}
} // namespace

void
nx769j_dvfs_start(void)
{
	gMode = 1;
	PE_parse_boot_argn("nx769j-dvfs", &gMode, sizeof(gMode));
	volatile uint8_t *fuse = reinterpret_cast<volatile uint8_t *>(ml_io_map(kQfprom, 0x2000));
	gSpeedBinByte = fuse[0x9b];
	for (unsigned id = 0; id < kDomains; id++) {
		scan(id);
	}
	nx769j_dvfs_report();
	if (gMode == 2) {
		for (unsigned id = 0; id < kDomains; id++) {
			request(id, gDomain[id].rows ? gDomain[id].rows - 1 : 0);
		}
	} else if (gMode == 1) {
		thread_t thread;
		if (kernel_thread_start(governor, nullptr, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
			gRunning = true;
		}
	}
}

void
nx769j_dvfs_touch(void)
{
	gTouchAt = mach_absolute_time();
}

void
nx769j_dvfs_report(void)
{
	for (unsigned id = 0; id < kDomains; id++) {
		const Domain &d = gDomain[id];
		if (!d.regs) {
			continue;
		}
		uint32_t state = (rd(d.regs, 0x20) & 0xff) * kXoKhz;
		printf("NX769J DVFS d%u cpu%s %s: rows %u %u-%u MHz boost %u, boot idx %u, req idx %u (%u MHz) state %u MHz "
		    "busy %u%% requests %u\n", id, kDomainCpus[id], d.why, d.rows, d.rows ? d.khz[0] / 1000 : 0,
		    d.rows ? d.khz[d.rows - 1] / 1000 : 0, d.boostKhz / 1000, d.initialIndex, rd(d.regs, 0x320),
		    d.rows ? d.khz[d.cur] / 1000 : 0, state / 1000, d.busy, d.requests);
	}
	printf("NX769J DVFS mode %u governor %s | GPU speed bin %u (fuse byte 0x%02x)\n", gMode, gRunning ? "on" : "off",
	    (gSpeedBinByte >> 4) & 7, gSpeedBinByte & 0xff);
}
