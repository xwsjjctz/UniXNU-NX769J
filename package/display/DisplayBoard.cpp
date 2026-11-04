// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board glue for the display taken from the houji port (SweetFramebuffer.cpp, SweetSurface.cpp).
//
// Those files expect a console mute flag from houji's console patch and the NativeAdreno presentation hooks. NX769J has
// neither: the text console keeps drawing, and there is no GPU, so every frame takes the CPU copy.
#include <stdint.h>

extern "C" {
// houji's console stops drawing while this is set; the NX769J console does not read it.
int houji_console_muted = 0;

// No GPU: no fence to wait for and no frame to bracket.
uint32_t native_adreno_present_capture_fence(void) { return 0; }
int native_adreno_present_begin_frame(uint32_t) { return 0; }
void native_adreno_present_end(void) {}
}
