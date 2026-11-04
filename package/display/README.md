# Board display

The built-in IOSurfaceRoot, IOMobileFramebuffer (AppleCLCD) and IOWatchdog that
take the place of com.apple.iokit.IOSurface, IOMobileGraphicsFamily and
AppleARMWatchdogTimer, which the OS profile leaves out of the kernelcache.
`scripts/build-nx769j-display.py` compiles them into the kernel.

## Provenance

These files come from the houji port (Xiaomi 14, SM8650, iOS 16),
`kernel/overlay/iokit/Kernel/arm/` at commit
`c4d962fb22babc434c73d4e1d00147710ab255ec`:

- SweetSurface.h, SweetSurface.cpp
- SweetFramebuffer.cpp
- SweetWatchdog.cpp
- HoujiFrameQueue.h, HoujiSurfaceAcquire.h, HoujiScaleRows.h, SurfaceUseCount.h

They carry no license header, and houji's COPYING.md says its per-file
licensing has not been audited. Their terms are unknown; settle them with the
houji authors before this repository is published.

## Changes for NX769J

- SweetFramebuffer.cpp: the Xiaomi 14 geometry guard is replaced by a check of
  the GOP framebuffer. iOS renders the iPhone X canvas (1125x2436) centred on
  the 1116x2480 panel; `nx769j_canvas=native` uses the panel size instead.
  AppleCLCD publishes canvas-width and canvas-height on its nub.
- DisplayBoard.cpp (new): the NativeAdreno hooks the
  houji sources expect; there is no GPU, so every frame is a CPU copy.

The screen scale comes from `/chosen/display-scale` in the OS profile, not from
these files: MobileGestalt reads it there and FrontBoard asserts when it is 0.
