# NX769J device evidence

Every entry here is one run on the unit itself, with the command that produced
the image, the image hashes and what the panel showed. The device is identified
by serial `FY233201030E` (chip unique id `0xEDE074DB`), firmware
`GEN_CN_NX769SV3.0.0B08MR6` and its Android 14 ABL.

The board has **no accessible serial console, no USB keyboard and no way to
export a log**, so the panel is the only output channel. Results are therefore
transcribed from photographs of the display and quoted verbatim below.

## Boot chain as observed

| Stage | Panel | Meaning |
| --- | --- | --- |
| firmware | logo, driver list | `SILICIUM_UEFI.fd` runs |
| menu | `version 0.1.0`, `config: \EFI\UniXNU\unixnu.toml` | UniXNU DXE driver runs |
| payload | `payload: initrd B3DFA000-B7FFF430 [EB58904D] fs 1` | the vendor_boot ramdisk mounts as FAT |
| entry | `1 UniXNU xnu` | the payload declares a boot entry |
| loader | `DARWINBOOT: hand-off to the kernel` | DarwinBoot receives the boot request |
| hand-off | top sixteenth of the panel green | `DarwinEnter` painted its marker, `ExitBootServices` succeeded |
| kernel | blue band below green | entry probe stage 1: kernel entry, MMU off |
| kernel | magenta band below blue | entry probe stage 2: just before the MMU is enabled |
| kernel | white and yellow strips below the hand-off text | `scripts/add-progress-strips.py`: post-MMU checkpoints |

The three bands alone do not locate a stall. Magenta is painted before the MMU
turns on, and the kernel installs its own `VBAR_EL1` before that point, so the
loader's red band cannot report anything after it and a panic stays invisible
until a console exists.

## Error progression, in the order the runs happened

Each error named the next missing piece; none of them named the actual cause.

| Panel | Cause | Fix |
| --- | --- | --- |
| `'UniXNU' returned: Unsupported` | `xnu` entries resolve through `UNIXNU_LOADER_PROTOCOL` and nothing installed it | add `DarwinBootPkg/Driver/DarwinBoot.inf` to the firmware |
| `'UniXNU' returned: Out of resources` | the loader wants one contiguous conventional run of `owned_memory_size` | see the memory section |
| `Synchronous Exception ... Data abort: Synchronous external abort`, `FAR 0x00000001001D0000` | memory above 4 GiB was described in the map and the firmware cannot reach it | stop describing it |
| `no boot entry found` | built from a tree without the unconditional `ConnectAll`, so the initrd volume never got a `SimpleFileSystem` | see `package/UniXNUPkg/Library/UniXNULib/Main.c` |
| `NO WINDOW 80000000 BIG RUN 12845000 @ B8000000` | 2 GiB of owned memory against 310 MiB of free run | 512 MiB window, 764.8 MiB of conventional memory |

## Memory

| Map | Conventional | Hand-off |
| --- | --- | --- |
| DXE heap only (`Kernel` reserved) | 508.8 MiB, largest run 0x12845000 | `NO WINDOW 80000000 BIG RUN 12845000` for a 2 GiB window |
| `Kernel` as `Conv` | 764.8 MiB | `owned 20000000` allocated, hand-off reached |
| any map with memory above 4 GiB described | - | external abort at `FAR 0x1001D0000` |

`python3 scripts/check-memory-map.py --dtb ../uefi-nx769j/Resources/DTBs/nx769j.dtb
--map firmware/Platforms/ZTE/nx769jPkg/Library/MemoryMapLib/MemoryMapLib.c --board board.toml`
reports `64 descriptors, 37 fixed reservations, 37 covering one, 0 unexplained`.

The map has to be lifted into the Mu checkout before it reaches a build:
`cp -R firmware/. ../uefi-nx769j/`.

## Hand-off summary, read off the panel

```
HANDOFF kernel va FFFFFFF003DD0000 pa A81D0000
ENTRY pa AB7D6488 owned 20000000
WORDS 1400001E 00000000 00000000 00000000
VIDEO base E3940000 row 4464 1116x2480 32 bpp
```

`kernel va` is the lowest segment, `__PRELINK_TEXT`, not `__TEXT`: a prelinked
kernelcache is placed by segment, and the entry words read back from RAM match
the `kernel.macho` inside `payload.img` byte for byte, which is the check that the
load is correct. Comparing against `out/<device>/<os>/<config>/kernel/kernel.macho`
compares against an unpatched copy and misleads.

## Payload

- 64 MiB payload in `vendor_boot`'s `vendor_ramdisk` at ramdisk address `0x40000000`.
- The stock header places the ramdisk at `0x01000000` and the device tree at
  `0x01f00000`, a 15 MiB window, so the address has to move for a larger payload.
- The same payload as a `boot` ramdisk never reaches the menu.
- `mformat -F` produces a 4 MiB FAT32 volume with 8030 clusters that the firmware
  cannot mount (`fs 0`); `mformat -T 8192 -h 64 -s 32` produces FAT16 that mounts.
- Repacking the stock ramdisk reproduces the stock `vendor_boot_a` byte for byte.

## Kernel probes

`payload.adt.kernel_args` is `-v` only: `serial=3` selects the Apple SoC UART,
which this board does not expose, and removing it changes nothing (same three
bars, same stall).

The panic screen and a stage-4 reset probe share the zero padding after `_start`
and do not both fit: with a probe armed the panic screen needs `0xa1f` bytes
against `0x970` remaining, and `scripts/patch-xnu-entry-probe.py` refuses.

Reset probes could not locate the stall. Every probe after `ml_parse_cpu_topology`
was past the stall and so never fired. One immediate reset before `arm_vm_init`
rebooted once on 2026-11-01, and the same images did not reboot when repeated on
2026-11-02, so that reboot is unexplained and is not evidence of reaching that
call.

Progress strips replaced them. The first strip image stopped at strip 7, the call
to `ml_parse_cpu_topology`. The cause is `unixnu,cluster-type` on a kernel with
`MAX_CPU_CLUSTERS` 1: the first P core gives `phys_cluster_id` 1 and fails
`assert(phys_cluster_id <= MAX_CPU_CLUSTER_PHY_ID)`. Removing the property from
the DTS moved the last strip from `ml_parse_cpu_topology` to the `set_mmu_ttb`
call in `arm_vm_init`, which was the last checkpoint in that image.

## Runs

| Date | boot.img sha256 (16) | vendor_boot | Panel result |
| --- | --- | --- | --- |
| 2026-10-31 | `b58be184180ec980` | 64 MiB payload | hand-off summary, green band |
| 2026-10-31 | `e9a9e712cbec09aa` | 64 MiB payload | green, blue, magenta, stall |
| 2026-10-31 | `d97fe9698e5d7632` | `vendor_boot_a_512m` | green, blue, magenta, stall |
| 2026-10-31 | `d97fe9698e5d7632` | payload without `serial=3` | unchanged |
| 2026-11-01 | `c07d17d10435472e` | `probe31`, reset 8 s after `_arm_vm_init` | no reset |
| 2026-11-01 | `c07d17d10435472e` | `probe31before`, immediate reset before `_arm_vm_init` | rebooted once, next boot held the bands |
| 2026-11-02 | `dc774b738e8be9a2` | `probe6`, reset 8 s after the first `bzero` in `_arm_vm_init` | no reset |
| 2026-11-02 | `dc774b738e8be9a2` | `probe6before`, immediate reset before that `bzero` | one logo hang, then no reset on the reflash |
| 2026-11-02 | `dc774b738e8be9a2` | `probe2before`, immediate reset before `_early_random` | no reset |
| 2026-11-02 | `c07d17d10435472e` | `probe31before`, exact repeat | no reset |
| 2026-11-02 | `dc774b738e8be9a2` | `strips` | strips 1, 2, 4 to 7; strip 3 is a branch not taken |
| 2026-11-02 | `dc774b738e8be9a2` | `nocluster`, no `unixnu,cluster-type` | strips 1, 2, 4 to 18, 20; strips 3 and 19 are branches not taken |
| 2026-11-02 | `dc774b738e8be9a2` | `strips2`, 26 checkpoints | every strip through `load_context` except `arm_auxkc_init`, a branch not taken; no console text |
| 2026-11-02 | `dc774b738e8be9a2` | `strips3`, bootstrap thread and panic reports | stops after `PE_init_iokit`; exception at PC `0xfffffff00780b790`, `OSMetaClass::postModLoad`, following `superClassLink` |
| 2026-11-02 | `dc774b738e8be9a2` | `strips4`, fault report | PC as above, FAR `0xfeedbeff`, ESR `0x96000005`, x21 `0xfeedbeef`, x26 167, class `NativeUfsBlock`; the storage bindings were unslid and `gMetaClass` landed in `munger_tests` |
| 2026-11-02 | `dc774b738e8be9a2` | `slidefix`, slid storage bindings and rebuilt drivers | past `PE_init_iokit` and `initialize_screen`; `panic("failed to get mix-n-match policy: %d")` from AppleImage4 during `mac_policy_initmach`, caller `0xfffffff0046ecf60`; no console text |
| 2026-11-02 | `dc774b738e8be9a2` | `img4`, AppleImage4 `/chosen` properties | same strips; `panic("failed to read boot nonce hash: secure boot level = %#llx: %d")`, caller `0xfffffff0046ecf8c`; `manifest-properties` had no `BNCH` |
| 2026-11-02 | `dc774b738e8be9a2` | `bnch`, 48-byte `BNCH` | same strips; `panic("failed to get previous stage manifest hash: %d")` from the nonce manager, caller `0xfffffff0046f06a0`; `/chosen/boot-manifest-hash` was missing |
| 2026-11-02 | `dc774b738e8be9a2` | `bmh`, 48-byte `boot-manifest-hash` | all 22 strips through `bsd_init`; `panic("Undefined kernel instruction: pc=%p instr=%x")` from `handle_uncategorized`; no console text |
| 2026-11-02 | `dc774b738e8be9a2` | `pcreport`, panic arguments on the panel | pc `0xfffffff004a80038`, instr `0`: the `IOAESAccelerator` constructor binding, recorded for iPadOS 17.7.11, pointed at zeroed memory |
| 2026-11-02 | `dc774b738e8be9a2` | `aes`, measured AES bindings | NativeAES starts; AppleImage4 `panic("performAES: %#x")` with `0xe00002c7`, kIOReturnUnsupported, for a hardware-key selector |
| 2026-11-02 | `dc774b738e8be9a2` | `aeskey`, software service for hardware-key selectors | AppleImage4 passes; about 30 s later `panic("AMFI: No PMGR?")` at `ConfigurationSettings.cpp:152`, caller `0xfffffff00471a67c`; no console text |
| 2026-11-02 | `dc774b738e8be9a2` | `nopmgr`, AMFI PMGR lookup skipped | `panic("Failed to mount root device")` at `IOKitBSDInit.cpp:971`, caller in `IOFindBSDRoot`; the kernel completed startup and timed out waiting for the root device; the OS image was not on the device |
