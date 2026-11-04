// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J (nubia RedMagic 9 Pro) memory map.
//
// Device regions are taken from this board's own firmware device tree
// (/reserved-memory of the vendor_boot DTB) instead of another board's
// xbl_config: the Xiaomi values overlapped the NX769J splash area, which the
// MMU overlap check rejects at boot. The UEFI-internal regions follow the
// Mu-Silicium layout and match FD_BASE 0xA7100000 / stack 0xA760D000.
#include <Library/MemoryMapLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>

STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {
  {"gunyah_hyp", 0x80000000, 0x00E00000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"cpusys_vm", 0x80E00000, 0x00400000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"xbl_aop_merged", 0x81A00000, 0x00260000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"aop_cmd_db", 0x81C60000, 0x00020000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"aop_tme_uefi", 0x81C80000, 0x00074000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"chipinfo", 0x81CF4000, 0x00001000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"smem", 0x81D00000, 0x00200000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"adsp_mhi", 0x81F00000, 0x00020000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"pvmfw", 0x824A0000, 0x00100000, AddMem, 0, 0x703C07, 0, WRITE_BACK_XN},
  {"global_sync", 0x82600000, 0x00100000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"tz_stat", 0x82700000, 0x00100000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"qdss", 0x82800000, 0x02000000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"qlink_logging", 0x84800000, 0x00200000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"mpss_dsm", 0x86B00000, 0x04900000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"mpss_dsm_2", 0x8B400000, 0x00800000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"mpss", 0x8BC00000, 0x0F400000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"q6_mpss_dtb", 0x9B000000, 0x00080000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"ipa_fw", 0x9B080000, 0x00010000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"ipa_gsi", 0x9B090000, 0x0000A000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"gpu_microcode", 0x9B09A000, 0x00002000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"spss", 0x9B0A0000, 0x001E0000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"spu_shared_0", 0x9B280000, 0x00060000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"spu_shared_1", 0x9B2E0000, 0x00020000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"camera", 0x9B300000, 0x00800000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"video", 0x9BB00000, 0x00800000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"cvp", 0x9C300000, 0x00700000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"cdsp", 0x9CA00000, 0x01400000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"q6_cdsp_dtb", 0x9DE00000, 0x00080000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"q6_adsp_dtb", 0x9DE80000, 0x00080000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"adspslpi", 0x9DF00000, 0x04080000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"FD_Reserved", 0xA7000000, 0x00100000, AddMem, 0, 0x703C07, 0, WRITE_BACK},
  {"UEFI_FD", 0xA7100000, 0x00300000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"UEFI_FD_Reserved", 0xA7400000, 0x00200000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"CPU_Vectors", 0xA7600000, 0x00001000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"Info_Blk", 0xA7601000, 0x00001000, AddMem, 0, 0x703C07, 6, WRITE_BACK_XN},
  {"MMU_PageTables", 0xA7602000, 0x00003000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"Log_Buffer", 0xA7605000, 0x00008000, AddMem, 0, 0x703C07, 6, WRITE_BACK_XN},
  {"UEFI_Stack", 0xA760D000, 0x00040000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"SEC_Heap", 0xA764D000, 0x0008C000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"Sched_Heap", 0xA76D9000, 0x00400000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"FV_Region", 0xA7AD9000, 0x00400000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"UEFI_RESV", 0xA7ED9000, 0x00127000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  // One allocatable window from 0xA8000000 to 0xD7E00000 (766 MiB). DarwinBoot
  // reserves owned_memory_size for XNU out of this, so it has to be a single
  // contiguous conventional run; the 1 GiB "Kernel" placeholder that used to sit
  // at the bottom of it was never claimed by any component.
  {"DXE_Heap", 0xA8000000, 0x2FE00000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},
  {"ramoops", 0xD7E00000, 0x00200000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"tz_merged", 0xD8000000, 0x00800000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"Display_Reserved", 0xE3940000, 0x02B00000, AddMem, 5, 0x703C07, 0, WRITE_THROUGH_XN},
  {"hwfence", 0xE6440000, 0x002DD000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"trust_ui_vm", 0xF3800000, 0x04400000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"oem_vm", 0xF7C00000, 0x04C00000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"llcc_lpi", 0xFF800000, 0x00600000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"IMEM_Base", 0x14680000, 0x0002A000, NoHob, 1, 0x2, 7, NS_DEVICE},
  {"IMEM_Cookie_Base", 0x146AA000, 0x00016000, AddDev, 1, 0x2, 7, NS_DEVICE},
  {"IPC_ROUTER_TOP", 0x00400000, 0x00100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"SECURITY_CONTROL", 0x00780000, 0x00007000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"QUP", 0x00800000, 0x00300000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"PRNG_CFG_PRNG", 0x010C0000, 0x0000C000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"CRYPTO0_CRYPTO", 0x01DC0000, 0x00040000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"CLK_TCSR_TCSR_REGS", 0x01F00000, 0x00100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"PERIPH_SS", 0x08800000, 0x00100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"USB", 0x0A600000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"AOSS", 0x0B000000, 0x04000000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"TLMM", 0x0F000000, 0x01000000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"SMMU", 0x15000000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"APSS_HM", 0x17000000, 0x02000000, AddDev, 1, 0x400, 11, NS_DEVICE},
  // RAM above 4 GiB. The abort at FAR 0x1001D0000 came from describing
  // 0x100000000, where SM8650 has no RAM: the next bank starts at 0x880000000.
  // Android's /proc/iomem on this board starts System RAM ranges at
  // 0x880000000, 0x8b0000000, 0x8c0000000 and 0xa00000000. This takes 4 GiB of
  // the 0x8c0000000 range, inside the bank both the 12 and the 16 GiB units
  // have (the houji port uses the same range on the same SoC). DarwinBoot
  // carves the 3 GiB owned_memory_size window out of it.
  {"High_RAM", 0x8C0000000, 0x100000000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},
};

STATIC CONST EFI_MEMORY_REGION_DESCRIPTOR mClockMmio[] = {
  {"NX769J_Clock_GCC", 0x00100000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"NX769J_Clock_CAMCC", 0x0AC00000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"NX769J_Clock_DISPCC", 0x0AE00000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"NX769J_Clock_GPUCC", 0x03D00000, 0x00100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"NX769J_Clock_VIDEOCC", 0x0AA00000, 0x00200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"NX769J_Clock_PCIE_RSCC", 0x01D00000, 0x00015000, AddDev, 1, 0x400, 11, NS_DEVICE},
};
STATIC CONST EFI_MEMORY_REGION_DESCRIPTOR mUfsMmio =
  {"NX769J_UFS", 0x01D80000, 0x00040000, AddDev, 1, 0x400, 11, NS_DEVICE};
STATIC EFI_MEMORY_REGION_DESCRIPTOR mExtraMap[ARRAY_SIZE (gMemoryDescriptor) + ARRAY_SIZE (mClockMmio) + 1];

VOID GetMemoryMap (OUT EFI_MEMORY_REGION_DESCRIPTOR **Map, OUT UINT8 *Count)
{
  *Map   = gMemoryDescriptor;
  *Count = (UINT8)ARRAY_SIZE (gMemoryDescriptor);

  if (FeaturePcdGet (PcdNx769jClockMmioEnable) || FeaturePcdGet (PcdNx769jUfsMmioEnable)) {
    CopyMem (mExtraMap, gMemoryDescriptor, *Count * sizeof (*mExtraMap));
    if (FeaturePcdGet (PcdNx769jClockMmioEnable)) {
      CopyMem (&mExtraMap[*Count], mClockMmio, sizeof (mClockMmio));
      *Count += ARRAY_SIZE (mClockMmio);
    }
    if (FeaturePcdGet (PcdNx769jUfsMmioEnable)) {
      CopyMem (&mExtraMap[(*Count)++], &mUfsMmio, sizeof (mUfsMmio));
    }
    *Map = mExtraMap;
  }
}
