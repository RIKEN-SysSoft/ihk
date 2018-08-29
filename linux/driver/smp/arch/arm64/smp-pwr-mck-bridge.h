/* smp-pwr-mck-bridge.h COPYRIGHT FUJITSU LIMITED 2018 */
#ifndef __SMP_PWR_MCK_BRIDGE_H__
#define __SMP_PWR_MCK_BRIDGE_H__

void ihk_pwr_set_retention_state_flag_address(const unsigned long* addr);
void ihk_pwr_clear_retention_state_flag_address(void);
int ihk_pwr_mck_request(void **handle);
int ihk_pwr_linux_to_mck(void *handle, int linux_cpu);
int ihk_pwr_mck_to_linux(void *handle, int mck_cpu);
int ihk_pwr_ipi_read_register(void *handle, int mck_cpu, u32 sys_reg, u64* value);
int ihk_pwr_ipi_write_register(void *handle, int mck_cpu, u32 sys_reg, u64 set_bit, u64 clear_bit);

#endif /*__SMP_PWR_MCK_BRIDGE_H__*/
