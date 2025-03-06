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

#ifndef __FRAMEWORK_SYSCTL_H__
#define __FRAMEWORK_SYSCTL_H__

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "framework_state.h"

struct framework_sysctl_t {
	struct sysctl_ctx_list framework_sysctl_ctx;
	struct sysctl_oid *oid_framework_tree;
	struct sysctl_oid *oid_framework_screen_tree;
	struct sysctl_oid *oid_framework_screen_power_tree;
	struct sysctl_oid *oid_framework_screen_battery_tree;
	struct sysctl_oid *oid_framework_power_tree;

	/* Reference to power config */
	struct framework_screen_power_config_t *power_config;

	/* Reference to state structure */
	struct framework_state_t *state;

	/* l - sysctl lock */
	struct mtx lock;
	
	/* (l) debug flag */
	uint8_t debug;
};

#define FRAMEWORK_SYSCTL_LOCK(x) mtx_lock(&(x)->lock);
#define FRAMEWORK_SYSCTL_UNLOCK(x) mtx_unlock(&(x)->lock);

/* Get current debug level */
uint8_t framework_sysctl_debuglevel(void);

/* initialize sysctls */
int framework_sysctl_init(struct framework_sysctl_t *fsp,
			  struct framework_screen_power_config_t *power_config,
			  struct framework_state_t *state);

/* destroy sysctls */
int framework_sysctl_destroy(struct framework_sysctl_t *fsp);

#endif /* __FRAMEWORK_SYSCTL_H__ */
