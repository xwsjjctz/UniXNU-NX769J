# Staged Mu-Silicium files for `uefi-nx769j`

These are the files that are new or changed for this board in a Mu-Silicium
checkout. They are staged here so the firmware design is reviewable and so
creating the `uefi-nx769j` checkout is a copy rather than an authoring task.

Nothing here is built in this repository; the tree is what gets lifted across.
`Mu_Basecore`, the edk2 checkout, is a separate submodule and has to be
initialised as well, because `firmware.py` builds against it rather than against
a tree of its own:

```sh
git clone https://github.com/UniXNU/uefi-sheng uefi-nx769j
cd uefi-nx769j && git checkout -b nx769j
cp -R ../UniXNU-NX769J/firmware/* .
git submodule update --init --depth 1 -- \
  Common/Mu Common/Mu_OEM_Sample Silicium-ACPI Mu_Basecore \
  Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl
```

`firmware/` is laid out as it lands in the checkout, so the copy is a plain
`cp -R`.

## Staged

Everything the package adds is here, so `cp -R firmware/* .` is the whole step.

`Platforms/ZTE/nx769jPkg` is the minimal package that boots this phone to the
iOS lock screen. It is not the vendor-DXE package this directory held before
(still in Git history at `f180541`): that one stops in the vendor drivers with
"CPU_Vectors region not present in cfg file". The minimal package starts from
`Platforms/Xiaomi/houjiPkg` of the houji port (Xiaomi 14, the same SM8650) and
keeps only what UniXNU needs: core DXE, the simple framebuffer, the USB keyboard
stack, the vendor SPMI and PMIC drivers, DarwinBoot and UniXNUDxe.

| Staged path | Origin |
| --- | --- |
| `Resources/Configs/nx769j.toml` | derived from `nx729j.toml`, `header_version` 4 |
| `Platforms/ZTE/nx769jPkg/{nx769j.dsc,nx769j.fdf,nx769j.dec}` | houji's `houji.*`, renamed, `LanaiPkg`, `SOC_TYPE = 2` |
| `Platforms/ZTE/nx769jPkg/Include/{APRIORI,DXE,RAM}.inc` | houji's, cut down; `SPMIDxe` and `PmicDxeLa` from `Binaries/nx769j` |
| `Platforms/ZTE/nx769jPkg/Include/{Library,Protocol}/HoujiRootRAM*.h` | houji's, unchanged |
| `Platforms/ZTE/nx769jPkg/Drivers/NoCapsuleRuntimeDxe/` | houji's, unchanged |
| `Platforms/ZTE/nx769jPkg/Library/MemoryMapLib/` | **this board's map** (see below) |
| `Platforms/ZTE/nx769jPkg/Library/ConfigurationMapLib/` | houji's, renamed |

The houji files carry no license header and houji's `COPYING.md` says its
per-file licensing has not been audited, as for `package/display/`.

`scripts/check-nx769j-firmware.py` was written for the vendor-DXE package and
reports problems that do not apply to this one; build with
`./unixnu build firmware --skip-preflight`.

## Not staged, and why

Both of these come out of the device's own firmware and are not redistributable,
so they are extracted onto the build machine rather than committed.

| File | How to produce it |
| --- | --- |
| `Resources/DTBs/nx769j.dtb` | `python3 scripts/extract-firmware-dtb.py --vendor-boot vendor_boot.img --tree 2 -o Resources/DTBs/nx769j.dtb`. Tree 2 is "Pineapple v2 SoC", which this board matches by `qcom,msm-id`; all four Pineapple trees carry the same reservation set, so the memory map does not depend on the choice. |
| `Binaries/nx769j/QcomPkg/Drivers/…` | `python3 scripts/extract-uefi-drivers.py --image uefi.img --layout <this checkout>`. All 52 modules the platform includes name come out of `uefi.img`, each written with its EDK II INF wrapper and depex. [`proprietary-files.txt`](../proprietary-files.txt) pins the hash of every one. |

`scripts/check-nx769j-firmware.py` reports exactly which of these are still
missing and whether they are fatal, and is what `./unixnu build firmware` runs
before EDK II. With both present it reports the tree as consistent with
`board.toml` and `52/52` vendor drivers.

## The memory map

`MemoryMapLib.c` keeps the UEFI regions under 4 GiB, one conventional
`DXE_Heap` run at `0xA8000000`, the device-tree reservations, and one RAM bank
above 4 GiB: `High_RAM`, 4 GiB at `0x8C0000000`. SM8650 has no RAM at
`0x100000000`; describing that address is what raised the synchronous external
abort (FAR `0x1001D0000`) earlier. `0x8C0000000` is inside the bank both the 12
and the 16 GiB units have, and the houji port uses the same range. DarwinBoot
takes `board.toml`'s `owned_memory_size` (3 GiB) for XNU out of it.

## The one line that is easy to get wrong

`nx729j.dsc` includes `KailuaPkg/KailuaPkg.dsc.inc`. That is SM8550. NX769J is
SM8650, whose Mu-Silicium SoC package is **`LanaiPkg`** — named after Qualcomm's
platform codename, not the part number. The staged DSC includes `LanaiPkg` and
sets `SOC_TYPE = 2` (`SM8650-AB`).

The device agrees: its baseband is `LANAI_GEN_PACK` and its trustzone is
`LANAIAAAAANAZT`, and `LanaiPkg.dsc.inc` declares the GIC bases
`0x17100000` / `0x17180000` that this board measures.

## Provenance

Every value in these files, and the values still open, are in
[`docs/UEFI-FIRMWARE.md`](../docs/UEFI-FIRMWARE.md). The working reference is
`Platforms/ZTE/nx729jPkg` in
[`UniXNU/uefi-sheng`](https://github.com/UniXNU/uefi-sheng), branch `sheng`.
