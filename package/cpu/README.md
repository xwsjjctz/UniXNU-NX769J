# CPU

- `SmpReport.cpp`: a heartbeat line with the per-core ArmCpu bring-up state, from step 1 (nub started) to step 7 (running), the PSCI `CPU_ON` result and the failure code. drivers/ArmCpu starts the secondary cores only with `sheng-smp=1`.
- `CpuDvfs.cpp`: the EPSS frequency governor for the four domains:

  | Domain | CPUs | Cores |
  |---|---|---|
  | 0 | cpu0-1 | A520 |
  | 3 | cpu2-4 | A720 |
  | 1 | cpu5-6 | A720 |
  | 2 | cpu7 | X4 |

  It reads the factory lookup table, keeps the non-boost rows, and samples the idle ticks of each core every 50 ms. It raises the frequency at once and lowers it after four low samples. For one second after a touch it keeps at least 75 % of the cap. It writes only the perf-state request; EPSS chooses the voltages.

  | `nx769j-dvfs` | Behaviour |
  |---|---|
  | `1` (default) | Governor |
  | `2` | Fixed at the highest non-boost row |
  | `0` | Report only |

  The heartbeat prints one line per domain. The same line carries the GPU speed-bin fuse (qfprom `0x221c2000 + 0x9b`, bits 6:4).

`scripts/build-nx769j-probes.py` builds both files. `patches/xnu/nx769j-probes.patch` starts the governor from `bsd_init` and prints the reports from the heartbeat.
