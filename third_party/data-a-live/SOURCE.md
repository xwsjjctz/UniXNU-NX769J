# data-a-live tools

These files are copied unchanged from [ShueiTsuki/data-a-live](https://github.com/ShueiTsuki/data-a-live) at commit
d2c7089b0a2e6da2b1e7e843337b503ec032b2c5. That repository puts its tools under the MIT licence (`LICENSE` here, from its `LICENSES/MIT.txt`).

| Here | Upstream | Use |
|---|---|---|
| `metal/adapt_ios16.py` | `tools/metal/adapt_ios16.py` | Edits UniXNU NativeMetal c6d29ca for iOS 16 |
| `metal/build_plugin.sh` | `tools/metal/build_plugin.sh` | Reference recipe for `scripts/build-nx769j-metal.sh` |
| `metal/mtltest.*` | `tools/metal/mtltest.*` | Metal device test program |
| `vktest/*.c, *.comp, *.vert, *.frag` | `tools/vktest/` | Vulkan test program (fill, triangle) |
| `m2v/*.patch` | `tools/metal/m2v/` | data-a-live's metal2vulkan runtime changes, applied after NativeMetal's bridge patches (adapt_ios16.py uses their fields) |
| `vktest/build_ios.sh` | `tools/turnip/build_ios.sh` | Reference recipe for the Turnip and vktest build |
