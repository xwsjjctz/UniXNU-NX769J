// Only this read-only memory block device supplies Block I/O. No physical disk controller.
!if $(WITH_PAYLOAD) == 1
  INF UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
!endif
