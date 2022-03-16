/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

uint32_t var_sdram_data = 10U;
uint32_t var_sdram_bss;
K_SEM_DEFINE(test, 0, 1);
const uint32_t var_sdram_rodata = 100U;

__in_section(custom_section, static, var) uint32_t var_custom_data = 1U;

extern void function_in_sram(int32_t value);
void function_in_custom_section(void);
void function_in_sram2(void)
{


	/* Print values from sram2 */
	printk("Address of function_in_sram2 %p\n", &function_in_sram2);
	printk("Address of var_sdram_data %p\n", &var_sdram_data);
	printk("Address of k_sem_give %p\n", &k_sem_give);
	printk("Address of var_sdram_rodata %p\n", &var_sdram_rodata);
	printk("Address of var_sdram_bss %p\n\n", &var_sdram_bss);

	/* Print values from sram */
	function_in_sram(var_sdram_data);

	printk("\n\n");
	printk("***** var_sdram_data=%d, expected 10\n", var_sdram_data);
	printk("***** var_sdram_bss=%d, expected 0\n", var_sdram_bss);
	printk("\n\n");

	/* Print values which were placed using attributes */
	printk("Address of custom_section, func placed using attributes %p\n",
	       &function_in_custom_section);
	printk("Address of custom_section data placed using attributes %p\n\n",
	       &var_custom_data);

	k_sem_give(&test);
}

__in_section(custom_section, static, fun) void function_in_custom_section(void)
{
	return;

}
