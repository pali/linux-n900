#ifndef _XT_CONDITION_H
#define _XT_CONDITION_H

#include <linux/types.h>

#define XT_CONDITION_MAX_NAME_SIZE 27

struct xt_condition_mtinfo {
	const char name[XT_CONDITION_MAX_NAME_SIZE];
	__u8 invert;
	__u32 value;

	/* Used internally by the kernel */
	void *condvar __attribute__((aligned(8)));
};

struct condition_tginfo {
	char name[XT_CONDITION_MAX_NAME_SIZE];
	__u8 padding;
	__u32 value;

	/* Used internally by the kernel */
	void *condvar __attribute__((aligned(8)));
};

#endif /* _XT_CONDITION_H */
