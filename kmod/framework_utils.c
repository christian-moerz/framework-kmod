/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025 Chris Moerz
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/queue.h>
#include <sys/conf.h>
#include <sys/types.h>

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

#include "framework_utils.h"

/*
 * Look up a character device node driver component
 * via its character device name
 */
void *
framework_util_lookupcdev_drv1(const char *devname)
{
	struct cdev_priv *cdp = NULL;

	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {
		if (*devname != *cdp->cdp_c.si_name)
			continue;

		if (0 == strcmp(devname, cdp->cdp_c.si_name))
			return cdp->cdp_c.si_drv1;
	}
	
	return NULL;
}

/*
 * Iterate through available character devices and match
 * all devices that start with devname
 *
 * Calls cbfunc with full device name and driver pointer
 * Stops iteration if cbfunc returns non-zero return value
 */
int
framework_util_matchcdev_drv1(const char *devname,
			      framework_cdev_cbmatch cbfunc,
			      void *ctx)
{
	struct cdev_priv *cdp = NULL;
	int result = 0;
	size_t name_len = strlen(devname);

	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {
		if (*devname != *cdp->cdp_c.si_name)
			continue;

		if (0 == strncmp(devname, cdp->cdp_c.si_name, name_len)) {
			if (cbfunc)
				result |= cbfunc(cdp->cdp_c.si_name, cdp->cdp_c.si_drv1, ctx);

			if (result)
				break;
		}
	}
	
	return result;
}
