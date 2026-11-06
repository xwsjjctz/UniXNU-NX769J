// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J Adreno 750 command processor start (nx769j-gpu=9), after GpuPower.cpp left the GMU running and GX on.
// Ported from UniXNU-sheng package/GpuPowerProbe.cpp gpuStart / gpuHwInit (2d4b58b7,
// SM8550 / A740), with the A750 branches of Linux v6.16 a6xx_gpu.c hw_init:
//   - no hwcg table: a6xx_set_hwcg takes the global clock-gating path (RBBM_CLOCK_CNTL_GLOBAL, CGC_GLOBAL_LOAD_CMD,
//     CGC_P2S_TRIG_CMD, wait CGC_P2S_STATUS.TXDONE, CLOCK_CNTL_GLOBAL 0) after the GMU CGC mode/delay/hysteresis writes
//   - RB_CMP_DBG_ECO_CNTL bit 19 (bit 11 is the a7xx default) and TPL1_DBG_ECO_CNTL1 0xc0700
// UBWC (a6xx_calc_ubwc_config, a740 family), CP protect (a730_protect), APRIV and the A7XX interrupt mask equal A740's.
//
// Steps, each reported as a line: GPU context (bank 0, ASID 2, SMR 0/1 for streams 0/1), SQE copy without its first dword,
// hw_init, ring 0 and the power-up list, CP_INIT with a fence, then CP_MEM_WRITE of a known word into the scratch page with a
// second fence. Interrupts are not attached: completion is polled.
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <libkern/c++/OSSharedPtr.h>
#include <stdarg.h>

extern "C" {
#include "AdrenoPM4.h"
extern const uint8_t native_adreno_sqe_firmware_image[];
extern const uint64_t native_adreno_sqe_firmware_image_size;
}
#include "NativeAdrenoGmuContext.h"
#include "NativeAdrenoGpuContext.h"
#include "NativeAdrenoRing.h"
#include "NativeAdrenoCpInit.h"
#include "NativeAdrenoPwrup.h"
#include "NativeAdrenoMemory.h"
#include "NativeAdrenoA740HwInit.h"
#include "GpuBridge.h"

namespace nx769j_gpu {
namespace {
// a6xx.xml (Linux v6.16), A7XX-only registers the A750 path uses beyond NativeAdrenoA740HwInit.h.
constexpr uint32_t kRbbmClockCntlGlobal = 0x000ad, kRbbmCgcGlobalLoadCmd = 0x0011e, kRbbmCgcP2sTrigCmd = 0x0011f;
constexpr uint32_t kRbbmCgcP2sStatus = 0x00122, kTpl1DbgEcoCntl1 = 0x0b602;
constexpr uint32_t kGmuCgcMode = 0x00020202;   // a6xx_catalog.c A750 gmu_cgc_mode

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char *fmt, ...)
{
	char text[168];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	report(text);
}

NativeAdrenoGmuContextReport gContext;
NativeAdrenoGpuBuffer gBuffers[GpuBufferCount];
OSSharedPtr<IOMemoryMap> gMaps[GpuBufferCount];
volatile uint8_t *gCpu[GpuBufferCount];
NativeAdrenoRing gRing;
NativeAdrenoCpInit gCpInit;

void contextProgress(uint32_t s) { progress(0x90 + s); }

// Context bank 0 base in the SMMU window: banks follow 2^(NUMPAGENDXB+1) global pages.
size_t bank0()
{
	const uint32_t id1 = gContext.id1;
	if (!id1) {
		return 0;
	}
	const size_t page = (id1 >> 31) ? 0x10000 : 0x1000;
	const size_t base = page << (((id1 >> 28) & 7) + 1);
	return base + 0x800 <= smmuBytes() ? base : 0;
}

uint32_t fsr()
{
	const size_t b = bank0();
	return b ? *reinterpret_cast<volatile uint32_t *>(smmu() + b + 0x58) : 0xdead;
}

// MMU-500 ACTLR of the GPU bank: CMTLB | PREFETCH_DEEP with CPRE off (Linux arm_mmu500_reset clears CPRE in every bank), then a
// full TLB invalidate (TLBIALL +0x618, TLBSYNC +0x7f0, TLBSTATUS +0x7f4).
void setActlr()
{
	const size_t b = bank0();
	if (!b) {
		return;
	}
	volatile uint32_t *base = reinterpret_cast<volatile uint32_t *>(smmu());
	base[(b + 0x4) / 4] = 0x301;
	__builtin_arm_dsb(0xf);
	base[(b + 0x618) / 4] = 0;
	base[(b + 0x7f0) / 4] = 0;
	for (unsigned i = 0; i < 1000 && (base[(b + 0x7f4) / 4] & 1); ++i) {
		IODelay(1);
	}
}

void wr64(uint32_t reg, uint64_t value)
{
	gpuWrite(reg, uint32_t(value));
	gpuWrite(reg + 1, uint32_t(value >> 32));
}

// a6xx_set_hwcg(gpu, true) for a GPU without a hwcg table (A750).
bool setHwcg(uint32_t *polls)
{
	gmuWrite(A740_GPU_GMU_AO_GMU_CGC_MODE_CNTL, kGmuCgcMode);
	gmuWrite(A740_GPU_GMU_AO_GMU_CGC_DELAY_CNTL, 0x10111);
	gmuWrite(A740_GPU_GMU_AO_GMU_CGC_HYST_CNTL, 0x5555);
	gpuWrite(kRbbmClockCntlGlobal, 1);
	gpuWrite(kRbbmCgcGlobalLoadCmd, 1);
	gpuWrite(kRbbmCgcP2sTrigCmd, 1);
	bool done = false;
	for (*polls = 1; *polls <= 10 && !done; ++*polls) {
		done = gpuRead(kRbbmCgcP2sStatus) & 1;   // TXDONE
		if (!done) {
			IODelay(1);
		}
	}
	gpuWrite(kRbbmClockCntlGlobal, 0);
	return done;
}

// hw_init (a6xx_gpu.c v6.16), A750 branches, up to CP_SQE_INSTR_BASE; OOB GPU_SET is held by GpuPower.cpp.
bool hwInit()
{
	gpuWrite(A740_GBIF_HALT, 0);
	(void)gpuRead(A740_GBIF_HALT);
	gpuWrite(A740_RBBM_GBIF_HALT, 0);
	(void)gpuRead(A740_RBBM_GBIF_HALT);
	gpuWrite(A740_RBBM_SECVID_TSB_CNTL, 0);
	wr64(A740_RBBM_SECVID_TSB_TRUSTED_BASE, 0);
	gpuWrite(A740_RBBM_SECVID_TSB_TRUSTED_SIZE, 0);
	uint32_t polls = 0;
	const bool cgc = setHwcg(&polls);
	gpuWrite(A740_RBBM_GBIF_CLIENT_QOS_CNTL, 0x2120212);
	gpuWrite(A740_UCHE_GBIF_GX_CONFIG, 0x10240e0);
	gpuWrite(A740_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);
	wr64(A740_UCHE_TRAP_BASE, 0x1fffffffff000ull);
	wr64(A740_UCHE_WRITE_THRU_BASE, 0x1fffffffff000ull);
	wr64(A740_UCHE_GMEM_RANGE_MIN, 0x1000000);                    // a740 family: SZ_16M
	wr64(A740_UCHE_GMEM_RANGE_MAX, 0x1000000 + 3 * 0x100000 - 1);  // A750 gmem 3 MiB
	gpuWrite(A740_UCHE_CACHE_WAYS, 1u << 23);
	gpuWrite(A740_CP_AHB_CNTL, 0x1);
	gpuWrite(A740_RBBM_PERFCTR_CNTL, 0x1);
	gpuWrite(A740_RBBM_INTERFACE_HANG_INT_CNTL, (1u << 30) | 0xcfffff);
	gpuWrite(A740_UCHE_CLIENT_PF, (1u << 7) | 0x1);
	gpuWrite(A740_CP_PROTECT_CNTL, (1u << 0) | (1u << 1) | (1u << 3));
	for (unsigned i = 0; i < NATIVE_ADRENO_A740_PROTECT_COUNT; ++i) {
		gpuWrite(A740_CP_PROTECT + NativeAdrenoA740Protect[i][0], NativeAdrenoA740Protect[i][1]);
	}
	gpuWrite(A740_UCHE_CMDQ_CONFIG, (6u << 16) | (6u << 12) | (9u << 8) | (1u << 3) | (1u << 2) | 2u);
	gpuWrite(A740_CP_APRIV_CNTL, 0x6f);
	gpuWrite(A740_CP_BV_APRIV_CNTL, 0x0f);
	gpuWrite(A740_CP_LPAC_APRIV_CNTL, 0x0f);
	// A750: disable UBWC merged UFC requests, TP flag hint and performance settings.
	gpuWrite(A740_RB_CMP_DBG_ECO_CNTL, gpuRead(A740_RB_CMP_DBG_ECO_CNTL) | (1u << 19));
	gpuWrite(kTpl1DbgEcoCntl1, 0xc0700);
	// a6xx_set_ubwc_config, a740 family: UBWC 4.0, hbb 16, AMSBC, rgb565 predicator, uavflagprd_inv 2, macrotile.
	gpuWrite(A740_RB_NC_MODE_CNTL, (1u << 11) | (1u << 4) | (3u << 1));
	gpuWrite(A740_TPL1_NC_MODE_CNTL, 3u << 1);
	gpuWrite(A740_SP_NC_MODE_CNTL, (2u << 4) | (3u << 1));
	for (uint32_t pipe = 1; pipe <= 2; ++pipe) {   // PIPE_BR, PIPE_BV
		gpuWrite(A740_CP_APERTURE_CNTL_HOST, pipe << 12);
		gpuWrite(A740_GRAS_NC_MODE_CNTL, 3u << 5);
	}
	gpuWrite(A740_CP_APERTURE_CNTL_HOST, 0);
	gpuWrite(A740_UCHE_MODE_CNTL, 3u << 21);
	gpuWrite(A740_RBBM_NC_MODE_CNTL, 1);
	gpuWrite(A740_RBBM_INT_0_MASK, 0x33d283c2);   // A7XX_INT_MASK; polled, not attached
	wr64(A740_CP_SQE_INSTR_BASE, gBuffers[0].iova);
	say("hw init: cgc %s (%u polls) clock-cntl %08x eco %08x status %08x", cgc ? "txdone" : "TIMEOUT", polls,
	    gpuRead(A740_RBBM_CLOCK_CNTL), gpuRead(A740_RB_CMP_DBG_ECO_CNTL), gpuRead(A740_RBBM_STATUS));
	return cgc;
}

int ringBarrier(void *)
{
	__builtin_arm_dsb(0xf);
	return 0;
}
// Ring, memptrs and lists are Normal non-cacheable on both sides (GPU MAIR 0, CPU write-combine): ordering only.
int ringPublish(void *, const volatile uint32_t *, size_t)
{
	__builtin_arm_dsb(0xf);
	return 0;
}
int ringAcquire(void *, volatile uint32_t *, size_t)
{
	__builtin_arm_dsb(0xf);
	return 0;
}
uint64_t ringNow(void *) { return nowUs(); }
int ringIdle(void *)
{
	IODelay(20);
	return 0;
}

void cpLine(const char *what, uint32_t step)
{
	const volatile uint32_t *mem = reinterpret_cast<const volatile uint32_t *>(gCpu[2]);
	say("cp %u %s: RBBM_STATUS %08x INT %08x HW_FAULT %x rptr/wptr %x/%x shadow %x fence %x smmu-fsr %x", step, what,
	    gpuRead(A740_RBBM_STATUS), gpuRead(A740_RBBM_INT_0_STATUS), gpuRead(A740_CP_HW_FAULT), gpuRead(A740_CP_RB_RPTR),
	    gpuRead(A740_CP_RB_WPTR), mem ? mem[0] : 0, mem ? mem[1] : 0, fsr());
}

} // namespace

bool
commandStart()
{
	// 1. The GPU's own context bank (arm-smmu-qcom gives the GPU bank 0; iommus streams 0 and 1).
	progress(0x90);
	if (!smmu()) {
		say("gpu context: no SMMU window");
		return false;
	}
	const IOReturn ctx = NativeAdrenoGpuContextBringup(smmu(), smmuBytes(), 0, 2, 0, 0, 1, 1, contextProgress, &gContext);
	say("gpu context: step %u status %x program %u/%u phase %u enabled %u route %u verified %u spans %u", gContext.step,
	    unsigned(ctx), gContext.program_status, gContext.route_status, gContext.program_phase, gContext.program_enabled,
	    gContext.route_phase, gContext.routes_verified, gContext.spans);
	if (ctx) {
		return false;
	}
	setActlr();
	if (NativeAdrenoGpuContextBuffers(gBuffers)) {
		say("gpu context: buffers refused");
		return false;
	}
	for (unsigned i = 0; i < GpuBufferCount; ++i) {
		IODeviceMemory *m = IODeviceMemory::withRange(gBuffers[i].physical, gBuffers[i].bytes);
		if (m) {
			gMaps[i] = OSSharedPtr<IOMemoryMap>(m->map(kIOMapAnywhere | kIOMapWriteCombineCache), OSNoRetain);
			m->release();
		}
		if (!gMaps[i]) {
			say("gpu context: buffer %u not mapped", i);
			return false;
		}
		gCpu[i] = reinterpret_cast<volatile uint8_t *>(gMaps[i]->getVirtualAddress());
	}
	// 2. SQE: adreno_fw_create_bo copies the firmware without its first dword.
	progress(0xb2);
	const uint64_t sqe = native_adreno_sqe_firmware_image_size;
	if (sqe < 8 || sqe - 4 > gBuffers[0].bytes) {
		say("sqe: %llu bytes do not fit the %llu-byte buffer", (unsigned long long)sqe, (unsigned long long)gBuffers[0].bytes);
		return false;
	}
	for (uint64_t i = 4; i < sqe; ++i) {
		gCpu[0][i - 4] = native_adreno_sqe_firmware_image[i];
	}
	__builtin_arm_dsb(0xf);
	for (uint64_t i = 4; i < sqe; ++i) {
		if (gCpu[0][i - 4] != native_adreno_sqe_firmware_image[i]) {
			say("sqe: readback differs at %llx", (unsigned long long)i);
			return false;
		}
	}
	// 3. hw_init.
	progress(0xb3);
	hwInit();
	// 4. Ring 0 (32 KiB), memptrs, BV rptr shadow, SQE start.
	progress(0xb4);
	const NativeAdrenoRingIo rio{nullptr, gpuWrite32, gpuRead32, ringBarrier, nullptr, nullptr};
	const NativeAdrenoRingCache cache{nullptr, ringPublish, ringAcquire};
	const IOReturn attached = gRing.attach(&rio, &cache, const_cast<uint8_t *>(gCpu[1]), gBuffers[1].bytes, gBuffers[1].iova,
	    const_cast<uint8_t *>(gCpu[2]), gBuffers[2].bytes, gBuffers[2].iova);
	if (attached) {
		say("ring: attach %x", unsigned(attached));
		return false;
	}
	wr64(A740_CP_BV_RB_RPTR_ADDR, gBuffers[2].iova + 8);
	gpuWrite(A740_CP_SQE_CNTL, 1);
	// 5. Power-up list (preemption registers; IFPC off) and CP_INIT.
	progress(0xb5);
	size_t written = 0;
	const IOReturn list = NativeAdrenoPwrupList::buildA740(gpuRead32, nullptr, const_cast<uint8_t *>(gCpu[3]),
	    gBuffers[3].bytes, &written);
	if (list) {
		say("pwrup list: %x", unsigned(list));
		return false;
	}
	__builtin_arm_dsb(0xf);
	const NativeAdrenoCpInitConfig config{gBuffers[0].iova, gBuffers[3].iova, 1};
	uint32_t fence = 0, seen = 0;
	const IOReturn init = gCpInit.run(&rio, &gRing, &config, &fence);
	if (init) {
		say("cp init: refused %x", unsigned(init));
		return false;
	}
	const NativeAdrenoRingClock clock{nullptr, ringNow, ringIdle};
	progress(0xb6);
	const uint32_t cpWait = gRing.wait(fence, &clock, 200000, &seen);
	cpLine(cpWait ? "CP_INIT fence timeout" : "CP_INIT done", 6);
	if (cpWait) {
		return false;
	}
	// 6. CP_MEM_WRITE (opcode 0x3d: address lo/hi, data) into the scratch page, then its fence.
	progress(0xb7);
	volatile uint32_t *scratch = reinterpret_cast<volatile uint32_t *>(gCpu[4]);
	scratch[0] = 0;
	__builtin_arm_dsb(0xf);
	uint32_t words[4] = {};
	if (AdrenoPM4Type7(0x3d, 3, &words[0]) != AdrenoPM4Success) {
		say("pm4: encode failed");
		return false;
	}
	words[1] = uint32_t(gBuffers[4].iova);
	words[2] = uint32_t(gBuffers[4].iova >> 32);
	words[3] = 0x5e9a750;
	if (gRing.submit(words, 4, &fence)) {
		say("mem write: submit refused");
		return false;
	}
	const uint32_t writeWait = gRing.wait(fence, &clock, 200000, &seen);
	const uint32_t word = scratch[0];
	const bool ok = !writeWait && word == 0x5e9a750;
	cpLine(ok ? "GPU WROTE MEMORY" : writeWait ? "mem write fence timeout" : "mem write missing", 7);
	say("mem write: scratch %08x (want 05e9a750) fence %u seen %u", word, fence, seen);
	return ok;
}
} // namespace nx769j_gpu
