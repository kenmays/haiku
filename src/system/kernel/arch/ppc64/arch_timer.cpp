/* PPC64 decrementer/timebase timer support. */
#include <boot/stage2.h>
#include <kernel.h>
#include <timer.h>
#include <arch/timer.h>

static uint64 sTimebaseFrequency;

void arch_timer_set_hardware_timer(bigtime_t timeout)
{
	if (timeout < 1000)
		timeout = 1000;

	uint64 ticks = (uint64)timeout * sTimebaseFrequency / 1000000ULL;
	if (ticks > 0x7fffffffULL)
		ticks = 0x7fffffffULL;
	asm volatile("mtdec %0" :: "r"((uint32)ticks));
}

void arch_timer_clear_hardware_timer()
{
	asm volatile("mtdec %0" :: "r"(0x7fffffffU));
}

int arch_init_timer(kernel_args* args)
{
	sTimebaseFrequency = args->arch_args.time_base_frequency;
	return sTimebaseFrequency != 0 ? B_OK : B_ERROR;
}
