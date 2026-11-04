// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board glue for the display taken from the houji port (SweetFramebuffer.cpp, SweetSurface.cpp).
//
// Those files expect the NativeAdreno presentation hooks. NX769J has no GPU, so every frame takes the CPU copy. The
// console mute flag they set (houji_console_muted) is defined in the kernel by nx769j-console-mute.patch.
#include <stdint.h>

extern "C" {
// No GPU: no fence to wait for and no frame to bracket.
uint32_t native_adreno_present_capture_fence(void) { return 0; }
int native_adreno_present_begin_frame(uint32_t) { return 0; }
void native_adreno_present_end(void) {}
}
