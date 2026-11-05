# Touch screen

The touch controller is a Goodix GT9916R (`goodix,brl-d`, Berlin-D).

- **`GoodixBus.cpp`**: the bus and power side.
  - The controller's SPI pins (GPIO 48–51) are run by hand as GPIOs. QUP1 SE4 has SPI firmware 0x102 with its FIFO interface disabled, which leaves only GSI DMA.
  - It votes L14B (3.2 V) on through the apps RSC, after checking the `ldob14` entry in the command DB.
  - It drives the reset pin (GPIO 161) and the IO supply switch (GPIO 28).
  - It latches falling edges on the interrupt pin (GPIO 162) in TLMM.
- **`GoodixTouch.cpp`**: the controller side.
  - It runs the Berlin handshake and reads the version and the IC info.
  - A kernel thread reads contacts after each interrupt and passes them to `sweet_hid_touch()` (package/hid) in canvas pixels. The conversion is panel = raw / 16, then canvas = panel + (4, -22).

`bsd_init` starts the driver through `patches/xnu/nx769j-probes.patch`. To turn it off, boot with `nx769j-touch=0`. The heartbeat prints the status lines and the frame counters.
