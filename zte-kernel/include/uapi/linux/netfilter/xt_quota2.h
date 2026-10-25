/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_QUOTA2_H
#define _XT_QUOTA2_H

#include <linux/types.h>

enum xt_quota_flags {
	XT_QUOTA_GROW         = 0x1,
	XT_QUOTA_PACKET       = 0x2,
	XT_QUOTA_NO_CHANGE    = 0x4,
	XT_QUOTA_INVERT       = 0x8,
};
#define XT_QUOTA_MASK (XT_QUOTA_GROW | XT_QUOTA_PACKET | XT_QUOTA_NO_CHANGE | XT_QUOTA_INVERT)

struct xt_quota_mtinfo2 {
	u_int64_t quota;
	char name[sizeof("foobar")];
	u_int8_t flags;
	/* Used internally by the kernel */
	struct xt_quota_counter *master;
};

#endif /* _XT_QUOTA2_H */
