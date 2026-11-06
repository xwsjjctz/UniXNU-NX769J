# GPU

Adreno 750 (`qcom,adreno-43051401`, gen7_9_1) bring-up on SM8650. The code runs in the kernel because the minimal UEFI
firmware has none of the vendor clock, RPMh or SCM drivers that houji's handoff path relies on.

- `GpuPower.cpp`: staged and gated, enabled with the boot-arg `nx769j-gpu=<mode>`. The boot-arg is unset by default.

  | Mode | Adds |
  |---|---|
  | 1 | Read the GCC and GPUCC state, the GPU SMMU and RBBM_STATUS |
  | 2 | `gcc_gpu_cfg_ahb_clk` |
  | 3 | CX GDSC plus the AHB and SMMU vote clocks |
  | 4 | The GMU's own 4K SMMU context (stream 5) |
  | 5 | GMU clock tree read; command DB turned into HFI tables |
  | 6 | GPLL0 votes, gpucc PLL0 and PLL1, hub 200 MHz, GMU 260 MHz |
  | 7 | GMU cold start and HFI startup |
  | 8 | HFI perf vote and OOB GPU_SET (GX on) |
  | 9 | `GpuCommand.cpp`: the GPU SMMU context, SQE, hw_init, ring 0, CP_INIT and one CP_MEM_WRITE |

  It is a port of UniXNU-sheng `package/GpuPowerProbe.cpp` (2d4b58b7, SM8550 / A740). Linux runs both GPUs through the same
  a740-family GMU and HFI code.
- `GpuPowerBoard.h`: the SM8650 GCC and GPUCC facts. They match SM8550 except for three values:
  - the CX GDSC hw_ctrl register (0x9168);
  - the PLL0/PLL1 L and alpha values;
  - the GMU clock (260 MHz).
- `ShengGmuVotes.c`: sheng's HFI vote builder. Its inputs are generated.
- `GpuCommand.cpp`: mode 9, the command processor. It sets up the GPU's own SMMU context (bank 0, ASID 2, streams 0 and 1),
  copies `gen70900_sqe.fw` without its first dword, runs the A750 branches of Linux v6.16 `hw_init` (global clock gating with
  the P2S handshake, `RB_CMP_DBG_ECO_CNTL` bit 19, `TPL1_DBG_ECO_CNTL1` 0xc0700), starts ring 0 with the power-up list and
  CP_INIT, then has the GPU write 0x05e9a750 into a scratch page. Completion is polled; no interrupt is attached.
- `GpuBridge.h`: the accessors `GpuPower.cpp` hands to `GpuCommand.cpp` after the GMU cold start.

`scripts/build-nx769j-gpu.py` does the following:

- Generates the GMU registers, the RSC/PDC setup, the OPP and BCM tables and the firmware arrays from
  `.cache/adreno-source` (Linux v6.16) and `.cache/gpu-firmware` (stock vendor/firmware). Both are pinned in
  `scripts/data/nx769j-adreno-inputs.json` and not committed.
- Compiles the needed Adreno740, AdrenoFirmware, ArmSmmu4K, AdrenoPM4 and Sm8550Platform sources from the UniXNU catalog.
  Adreno740 is copied to `build/nx769j-gpu/adreno740` first and `patches/drivers/Adreno740/*.patch` applied to the copy.
  `gen70900-sqe-size.patch` raises `GpuSqeBytes` to 0x16000: the A750 SQE is 85360 bytes and A740's buffer holds 77824.

The device result is in `docs/DEVICE-EVIDENCE.md` (`gpu1`, `gpu2`, `gpu3`): the GMU runs, GX powers on and the command
processor executes a memory write. iOS does not use the GPU yet: there is no user-client service or Metal plugin.
