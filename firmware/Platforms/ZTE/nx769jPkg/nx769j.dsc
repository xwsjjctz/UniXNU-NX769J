##
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = nx769j
  PLATFORM_GUID                  = 777656F0-55E0-558F-B5AB-3976C89A40F2
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/nx769jPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = nx769jPkg/nx769j.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0
  DEFINE WITH_DARWINBOOT          = 1
  DEFINE WITH_PAYLOAD             = 1

  #
  # 0 = SM8650-AA
  # 1 = SM8650-Q-AA
  # 2 = SM8650-AB
  # 3 = SM8650-Q-AB
  # 4 = SM8650-AC
  #
  SOC_TYPE                       = 2

!include LanaiPkg/LanaiPkg.dsc.inc

[BuildOptions]
!if $(WITH_HOUJI_HIGH_RAM) == TRUE
  # Automatic pool/page allocations stay compatible with OEM 32-bit addresses.
  *_CLANGPDB_AARCH64_CC_FLAGS = -D HOUJI_LOW_UEFI_ALLOCS=1
!endif

[PcdsFixedAtBuild]
!if $(WITH_HOUJI_HIGH_RAM) == TRUE
!if $(TARGET) == DEBUG
  # Preserve debug/assert behavior, but FreePages must not poison gigabytes.
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x27
!endif
!endif
  # No SetupBrowser/driver-health forms in the RAM-only first-entry image.
  # UefiBootManagerLib documents ZeroGuid as the switch to skip that UI.
  gEfiMdeModulePkgTokenSpaceGuid.PcdDriverHealthConfigureForm|{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"nubia"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"nx769j"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"RedMagic 9 Pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"NX769J"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"nx769j"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1116
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2480
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|nx769jPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|nx769jPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

  DeviceBootManagerLib|SiliciumNullPkg/Library/PlatformBootManagerLibNull/PlatformBootManagerLibNull.inf
  PlatformBootOptionsLib|SiliciumNullPkg/Library/PlatformBootOptionsLibNull/PlatformBootOptionsLibNull.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf

[Components]
  Binaries/nx769j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  Binaries/nx769j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  Binaries/nx769j/QcomPkg/Drivers/EnvDxe/EnvDxe.inf
  nx769jPkg/Drivers/NoCapsuleRuntimeDxe/NoCapsuleRuntimeDxe.inf
!if $(WITH_DARWINBOOT) == 1
  # Build the loader separately for the RAM payload; not part of the stage-1 FV.
  DarwinBootPkg/Driver/DarwinBoot.inf
!endif
!if $(WITH_PAYLOAD) == 1
  UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
!endif
  #
  # ACPI Tables
  #
  #houji/AcpiTables.inf
