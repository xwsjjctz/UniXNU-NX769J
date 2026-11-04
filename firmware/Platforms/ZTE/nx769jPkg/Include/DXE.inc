// Stage-1 firmware modules. Variables use the emulated RAM store.
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF Binaries/nx769j/QcomPkg/Drivers/EnvDxe/EnvDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF nx769jPkg/Drivers/NoCapsuleRuntimeDxe/NoCapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  # USB input: XHCI host controller, then the bus and the keyboard driver.
  INF MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  # Vendor SPMI/PMIC stack: probe only. The keypad driver would sit on top of
  # these two; if they do not load, the volume keys are not reachable from UEFI.
  INF Binaries/nx769j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/nx769j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  # The XNU loader. UniXNU's `xnu` entry type needs UNIXNU_LOADER_PROTOCOL,
  # which only this driver installs; without it the entry returns Unsupported.
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  INF UniXNUPkg/Application/UniXNU/UniXNU.inf
