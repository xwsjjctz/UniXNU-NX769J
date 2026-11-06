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
| 2026-11-02 | `dc774b738e8be9a2` | `console`, `-v` forces the text console, `debug=0x8` | first kernel log on the panel: eight CPUs, IOKit, APFS, AMFI and Sandbox up, then `Waiting on` the root volume UUID and the same panic; no `NativeUfs` output because the driver needs `native-ufs-root` or `native-ufs-probe` |
| 2026-11-02 | `dc774b738e8be9a2` | `ufsprobe`, `native-ufs-probe=10` | `NATIVE_UFS root check`: controller up and GPT read; `userdata first=326328 last=3aabffa`, block0 `92eaf550 276e899c`, manifest status 1, windows 0. `first` matches `device-inputs.md`; `last` does not (`0x7641ffa` recorded); `/data` measures 222.09 GiB, which matches the probe |
| 2026-11-03 | `dc774b738e8be9a2` | `root`, `native-ufs-root=1`, trust cache bundle, truncated `vendor_boot_a` written first | trust caches load; `root preparation failed phase=1 window=0` because the policy table was empty |
| 2026-11-03 | `dc774b738e8be9a2` | `policy`, owner-signed header policies, hash tables in the image | `NativeUfs: published` SystemOS and OS, APFS mounts `SydneyF20F66.D22D22101S01`; `panic("rootvp not authenticated after mounting")` at `bsd_init.c:1079` |
| 2026-11-03 | `dc774b738e8be9a2` | `rootok`, read-only owner-verified root accepted in `bsd_init` | `NX769J: accepting owner-verified read-only root, FSIOC_KERNEL_ROOTAUTH returned 80`; `load_init_program` loads `/sbin/launchd`, then the panel stops after `shared_region ... check_np ... vm_shared_region_start_address() failed`: no shared cache, because SystemOS is not mounted |
| 2026-11-03 | `dc774b738e8be9a2` | `cache`, owner root trust and SystemOS union mount | root accepted by the owner check (no fallback message); init mounts `SydneyF20F66.D2D221SystemCryptex` (`disk1s1`, vol-uuid `556915A0-...`) without a panic; `launchd` loads, prints the same `check_np` line, then the panel stays silent. `Failed to open swap file 30` before it |
| 2026-11-03 | `dc774b738e8be9a2` | `trace`, exec/exit printf, 15 s heartbeat, `launchd_unsecure_cache=1` | exec `/usr/appleinternal/sbin/launchd.development` returns 2, exec `/sbin/launchd` returns 0 with `exec process signature accepted`; then no exec and no exit for 134 s; heartbeat continues with `nprocs 1`. The kernel is alive and launchd never spawns a child |
| 2026-11-03 | `dc774b738e8be9a2` | `sample`, launchd thread sampler | one thread, `state 0x4` (running), user pc `dyld+0x11990` just after the `svc` of `_kernelrpc_mach_vm_allocate_trap`, identical on every sample |
| 2026-11-03 | `dc774b738e8be9a2` | `irq`, trap arguments and interrupt PC ring | `vm_allocate(0x203, 0x16ed66590, 0x41000, 0x3c000001)`; interrupts land in `_bcopyin` (from `mach_vm_allocate_trap`), `sleh_synchronous`, `arm_fast_fault_internal`: the 8-byte copyin of the address argument faults forever. The kernel has no PAN write at all (`CFLAGS_EXTRA` never reaches xnu-8796, so `-DXNU_NATIVE_PAN=1` is lost) while `SCTLR_EL1.SPAN=0` lets the SM8650 cores set PAN on every exception entry; `arm_fast_fault` finds the PTE sufficient and redrives. Fix: `patches/xnu/nx769j-span-without-pan.patch` |
| 2026-11-03 | `dc774b738e8be9a2` | `span`, `SCTLR_EL1.SPAN=1` (`0x34d4593d`) | launchd runs its boot tasks: `NSUEarlyBootTask` exits 0, `cc_fips_test` (pid 10) passes every FIPS POST, then `keybagd` (pid 11): `MKB_INIT: FATAL KEYBAG ERROR: failed to load system bag`, `Could not save value:e00002c7`, `REBOOTING INTO RECOVERY MODE`. During the unmounts of that reboot: `panic: vnode_rele_ext: vp ... usecount -1 ... @vfs_subr.c` after unmounting the SystemOS union. No SEP and no AppleSEPKeyStore in the kernelcache |
| 2026-11-03 | `dc774b738e8be9a2` | `aks`, AppleKeyStoreState stand-in, `sheng-aks=1` | `NX769J AKS: open 1`, selectors 0 and 14 answer 0, `MKB_INIT: System Keybag loaded`; then `Could not open /private/var/keybags/systembag.kb`, `systembag.kb.writing: No such file or directory`, `FATAL KEYBAG ERROR: kb_load`, reboot into recovery. Earlier on the panel: `mount: data volume missing`. `/private/var` is the read-only OS volume |
| 2026-11-03 | `dc774b738e8be9a2` | `data`, 32 MiB packed APFS Data seed (`09A4C386-...`) in RAM through RamBlock, mounted on `/private/var` | launchd brings up 129-131 processes (`xpcproxy`, `cfprefsd`, `containermanagerd`, `CommCenter`, `homed`, ...); AKS selectors 0, 7, 14, 17 answer 0. SpringBoard starts (pid 170, 186, 192) and dies each time with `status 0x5 reason 2/0x5` (SIGTRAP), `Too many corpses being created`. The Data volume fills: `disk1s1 ... ENOSPC`, `No space left on device(28)`. `amfid` exits with `reason 1/0x4` (jetsam) |

| 2026-11-03 | `dc774b738e8be9a2` | `persist`, 4 GiB seed served from a UFS window (`sheng-data-persist=2 sheng-data-reset=1`), watchdogd in the seed's launchd `disabled.plist` | no more ENOSPC; the system runs for minutes, then `MACH Reboot` |
| 2026-11-03 | `dc774b738e8be9a2` | `display`, houji's SweetSurface and SweetFramebuffer as the board display, IOSurface/IOMobileGraphicsFamily/IOGPUFamily/AppleM2ScalerCSCDriver left out, `/arm-io/disp0` | `MobileGestaltHelper` and `backboardd` open AppleCLCD; selectors 8, 28, 57, 15, 22, 29, 33, 79, 74 all answer 0. ~60 s in, `watchdogd` (pid 50) exits `reason 2/0x5` and the kernel panics `watchdogd[...] exited`: the seed's `disabled.plist` is not honoured, most likely because its owner is not root |
| 2026-11-03 | `dc774b738e8be9a2` | `wdt`, houji's IOWatchdog, AppleARMWatchdogTimer left out | watchdogd still exits `reason 2/0x5` before it opens IOWatchdog (no `SWEET WDT` selector logged) |
| 2026-11-03 | `dc774b738e8be9a2` | `wdt2`, watchdogd's exit is not fatal (`nx769j-watchdogd-not-fatal.patch`) | no more `MACH Reboot`: 7 minutes, ~150 processes. SpringBoard dies with SIGTRAP every ~15 s without ever opening AppleCLCD; `mediaserverd` creates IOSurfaces |
| 2026-11-03 | `dc774b738e8be9a2` | `brk`, user brk report (pc, lr, frames, cache slide) | resolved against the iOS 16.5 dyld cache: `-[FBSDisplayMode _initWithWidth:...validityCheck:]` from `-[FBSDisplayConfiguration _initWithImmutableDisplay:...]` from `+[FBDisplayManager sharedInstance]` |
| 2026-11-03 | `dc774b738e8be9a2` | `brkmsg`, crash text at x0 | `FBSDisplayMode cannot be configured as requested : <FBSDisplayMode: ...; infxinf@0x (1116x2480/0) 60Hz p3 SDR>`: scale 0 |
| 2026-11-03 | `dc774b738e8be9a2` | `canvas`, iPhone X canvas 1125x2436 centred on the panel | same assertion at `(1125x2436/0)`: the scale does not come from the size. FrontBoard takes the main display's scale from CADisplayMode.preferredScale, QuartzCore from MobileGestalt `main-screen-scale`, and MobileGestalt from `/chosen/display-scale`, which iBoot normally writes |
| 2026-11-04 | `dc774b738e8be9a2` | `scale`, `/chosen/display-scale = 3`, `display-rotation = 0` | **the Apple logo is drawn**: backboardd creates a 1125x2436 BGRA surface and swaps frames into AppleCLCD. Then jetsam kills daemons by the dozen (`status 0x9 reason 1/0x4`, process thrashing): the 512 MiB memory window is the next limit |
| 2026-11-04 | `8487411d4e72a532` | `highmem`, minimal firmware with `High_RAM` (4 GiB at `0x8C0000000`), `owned_memory_size = 0xC0000000` | UEFI, DarwinBoot and XNU run from the high bank; APFS mounts, then `write_pte_fast: PTE write is unaligned` at `load_init_program`: with memSize above 1 GiB `arm_init` picked 16K pages, because `apple_arm64_common.h` defines `APPLE_ARM64_ARCH_FAMILY` and the 4K guard never fired |
| 2026-11-04 | `8487411d4e72a532` | `highmem4k`, `nx769j-native-4k-page-shift.patch` | no jetsam kills; 152 processes at 105 s; backboardd creates 1125x2436 surfaces and swaps frames; only CommCenter, watchdogd, fairplayd and bluetoothd keep exiting |
| 2026-11-04 | `8487411d4e72a532` | `noconsole`, `nx769j-console-mute.patch`, `sweetfb_noconsole=1` | the text console stops at the first swap; iOS's own Apple logo stays. Data read back over EDL: SpringBoard runs and never crashed, installd not yet spawned |
| 2026-11-04 | `8487411d4e72a532` | `wifimac`, `/chosen` `mac-address-wifi0` and `mac-address-bluetooth0` | after a long wait: black screen with status bar and home indicator. Data read-back: Setup killed by the FrontBoard `scene-create` watchdog (`0x8badf00d`, 20 s) while the single CPU was at 100 % (lsd, SpringBoard, analyticsd, containermanagerd), `IconState.plist` written. Whether the MAC addresses changed anything is not isolated |
| 2026-11-04 | `8487411d4e72a532` | `skipsetup`, Data seed from UniXNU `scripts/seed-data-skip-setup.py --build=20F66 --no-auto-dim` | **the iOS 16 lock screen**: date, clock, camera button and home indicator; the wallpaper fills in over a minute (black, white, then a blue gradient). No touch, battery shows empty |
| 2026-11-05 | `8487411d4e72a532` | `touchprobe`, GCC vote of the QUP1 SE4 clocks and a GENI SPI read of the touch controller (`nx769j-touch-probe=1`) | clocks come up (`cbcr 0 0 0 0 0`), every touch pin is still a GPIO with pull-down; SE4 runs SPI firmware `0x102` with `if_disable 0x1` (FIFO interface off, GSI DMA only), so `geni_spi_init` returns `-95`. houji recorded the same state on SM8650 |
| 2026-11-05 | `8487411d4e72a532` | `touch2`, GPIO SPI on pins 48-51 and an RPMh vote of `ldob14` | without the vote the Berlin handshake reads all `00`: L14B is off at boot. cmd-db gives `ldob14 0x41600 slave 4`; the probe expected slave 3 (ARC) and refused to vote. VRM is slave 4 (`include/soc/qcom/cmd-db.h`) |
| 2026-11-05 | `8487411d4e72a532` | `smp2`, `sheng-smp=1 armcpu-spi-ipi=1`, VRM slave fixed | **eight CPUs online**: `cpus 8 avail 8`, all seven secondaries at ArmCpu step 7 with PSCI `CPU_ON` returning 0; 152 processes at 66 s against 105 s on one core, and the lock screen wallpaper loads. RPMh vote of L14B completes on TCS 0 (`10101` x3), handshake `aa` x8, version block `BERLIN`, patch `9916R` |
| 2026-11-05 | `8487411d4e72a532` | `touch3`, Goodix driver on GPIO SPI feeding houji's in-kernel SweetHID, `com.apple.iokit.IOHIDFamily` left out of the prelink | **touch works** on the lock screen and home screen. Frame rate stays low: every frame is still a CPU copy |
| 2026-11-05 | `8487411d4e72a532` | `dvfs`, EPSS load-following governor (`package/cpu/CpuDvfs.cpp`, default `nx769j-dvfs=1`) | the UI is clearly smoother (user report). Before this the frequency domains kept the boot chain's request. Also: booting stock Android to charge reformatted userdata (`no valid root manifest`), so `userdata-root.img` was reflashed |
| 2026-11-05 | `8487411d4e72a532` | `gpu1`, Adreno 750 bring-up modes 1-8 (`nx769j-gpu=8`, package/gpu) | stages 1-6 pass: CX GDSC on, GPU SMMU readable (`scr0 002b0406 id0 4c017e10 id2 00005511`, no valid SMR), GMU SMMU context ready (bank 1, stream 5, 5 spans), PLL1 locked, hub 200 MHz and GMU 260 MHz running (`gcc_gpu_memnoc_gfx_clk` still reads CLK_OFF), cmd-db parsed into the HFI tables. Cold start stops at `status 4 phase 1`: the generated header subtracted the GPU-to-GMU delta from v6.16's GMU-relative register numbers |
| 2026-11-05 | `8487411d4e72a532` | `gpu2`, GMU register numbering fixed | **GMU firmware runs and GX powers on**: `cold start: status 0 phase 7 hash 1 fw-init 100 ready fw1 hfi1`, GMU state 1/0/0/0 > 0/1/0/1, HFI startup acked (`protocol 0 stage 8`, 4 commands, 1 ms), `GX ON: vote 0/0 perf 1 bw 1 oob-polls 2 gx 3>0 RBBM_STATUS 00000001`. iOS still renders on the CPU (no Metal path). New with this image: frequent screen corruption, missing wallpaper, black stalls; not yet separated from the GPU being on |
| 2026-11-05 | `8487411d4e72a532` | `gpu3`, mode 9 (`nx769j-gpu=9`, `GpuCommand.cpp`, `gen70900-sqe-size.patch`) | **the command processor runs and the GPU writes memory**: `mode 9 stage fa`; `gpu context: step 7 status 0 ... verified 2 spans 11`; `hw init: cgc txdone (2 polls) ... status 00000001`; `cp 6 CP_INIT done: ... HW_FAULT 0 rptr/wptr 11/11 shadow 11 fence 1`; `mem write: scratch 05e9a750 (want 05e9a750) fence 2 seen 2`. Open: `smmu-fsr 400` stays set in the GPU bank although the write lands; `clock-cntl deafbead` is expected (A750 uses the global clock gating, not RBBM_CLOCK_CNTL). The screen corruption and black stalls of `gpu2` did not recur; the UI is the same as CPU rendering |

The `data` run boots with `-v debug=0x8 native-ufs-root=1 launchd_unsecure_cache=1 sheng-aks=1`, written into the payload's
`unixnu.toml` after `unixnu build payload` (see the comment on `kernel_args` in `board.toml`).
