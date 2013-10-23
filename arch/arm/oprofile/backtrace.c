/*
 * Arm specific backtracing code for oprofile
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * Based on i386 oprofile backtrace code by John Levon, David Smith
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

#include "../kernel/stacktrace.h"

static int report_trace(struct stackframe *frame, void *d)
{
	unsigned int *depth = d;

	if (*depth) {
		oprofile_add_trace(frame->lr);
		(*depth)--;
	}

	return *depth == 0;
}

static void **user_backtrace(struct pt_regs * const regs,
				void **frame, int step)
{
	void *frame_data[4];
	int   instr;

	void *ret_addr;
	void **next_frame;

	if (!access_ok(VERIFY_READ, frame - 3, sizeof(frame_data)))
		return 0;
	if (__copy_from_user_inatomic(frame_data, frame - 3,
						sizeof(frame_data)))
		return 0;

	if (access_ok(VERIFY_READ, (int *)frame_data[3] - 2, sizeof(instr)) &&
		__copy_from_user_inatomic(&instr, (int *)frame_data[3] - 2,
						sizeof(instr)) == 0 &&
					(instr & 0xFFFFD800) == 0xE92DD800) {
		/* Standard APCS frame */
		ret_addr = frame_data[2];
		next_frame = frame_data[0];
	} else if (step != 0 ||
		(unsigned long)frame_data[2] - (unsigned long)regs->ARM_sp <
		(unsigned long)frame_data[3] - (unsigned long)regs->ARM_sp) {
		/* Heuristic detection: codesourcery optimized normal frame */
		ret_addr = frame_data[3];
		next_frame = frame_data[2];
	} else {
		/* Heuristic detection: codesourcery optimized leaf frame */
		ret_addr = (void *)regs->ARM_lr;
		next_frame = frame_data[3];
	}

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (next_frame <= frame)
		return NULL;

	oprofile_add_trace((unsigned long)ret_addr);

	return next_frame;
}

void arm_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	int step = 0;
	void **frame = (void **)regs->ARM_fp;

	if (!user_mode(regs)) {
		unsigned long base = ((unsigned long)regs) & ~(THREAD_SIZE - 1);
		walk_stackframe(regs->ARM_fp, base, base + THREAD_SIZE,
				report_trace, &depth);
		return;
	}

	while (depth-- && frame && !((unsigned long) frame & 3))
		frame = user_backtrace(regs, frame, step++);
}
