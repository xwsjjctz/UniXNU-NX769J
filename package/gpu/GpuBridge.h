// SPDX-License-Identifier: BSD-2-Clause-Patent
// What GpuPower.cpp (power, GMU, GX) hands to GpuCommand.cpp (the command processor), valid after the GMU cold start.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace nx769j_gpu {
void report(const char *text);                       // a status line, kept for the heartbeat
void progress(unsigned stage);                       // the stage number printed before a step that may hang
int gpuRead32(void *, uint32_t dword, uint32_t *value);   // GPU register window (dword offsets of a6xx.xml), ring I/O shape
int gpuWrite32(void *, uint32_t dword, uint32_t value);
uint32_t gpuRead(uint32_t dword);
void gpuWrite(uint32_t dword, uint32_t value);
void gmuWrite(uint32_t gpuDword, uint32_t value);    // a GMU register numbered from the GPU base (0x23b09 style)
volatile uint8_t *smmu();                            // the whole adreno SMMU window, mapped by stage 4
size_t smmuBytes();
uint64_t nowUs();
// Mode 9: GPU context, SQE, hw_init, ring, CP_INIT, one CP_MEM_WRITE.
bool commandStart();
} // namespace nx769j_gpu
