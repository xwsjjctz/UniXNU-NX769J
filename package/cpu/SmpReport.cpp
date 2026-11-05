// SPDX-License-Identifier: BSD-2-Clause-Patent
// Secondary CPU bring-up report for the heartbeat: what XNU counts and what drivers/ArmCpu recorded per core.
//
// ArmCpu starts the seven secondary cores only with sheng-smp=1. Its per-core step: 1 nub started, 2 registered,
// 3 start thread running, 4 PSCI CPU_ON issued, 5 entered initCPU, 6 GIC redistributor ready, 7 running. The PSCI
// return code and the failure code show where a core stopped.
#include <stdint.h>
#include <stddef.h>
#include <IOKit/IOLib.h>

extern "C" {
extern unsigned int processor_count;
extern uint32_t processor_avail_count;
extern volatile uint32_t armcpu_smp_requested, armcpu_smp_running, armcpu_smp_conduit, armcpu_boot_mpidr;
extern volatile uint32_t armcpu_smp_step[8], armcpu_smp_mpidr[8], armcpu_smp_fail[8];
extern volatile int32_t armcpu_smp_psci_rc[8];
void nx769j_smp_report(void);
}

void
nx769j_smp_report(void)
{
	char text[200];
	size_t at = (size_t)snprintf(text, sizeof(text), "cpus %u avail %u | smp req %u run %u conduit %u boot 0x%x |",
	    processor_count, processor_avail_count, armcpu_smp_requested, armcpu_smp_running, armcpu_smp_conduit,
	    armcpu_boot_mpidr);
	for (unsigned i = 1; i < 8 && at < sizeof(text); i++) {
		at += (size_t)snprintf(text + at, sizeof(text) - at, " %u:%u/%d/%u", i, armcpu_smp_step[i], armcpu_smp_psci_rc[i],
		    armcpu_smp_fail[i]);
	}
	printf("NX769J SMP %s\n", text);
}
