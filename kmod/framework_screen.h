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

#ifndef __FRAMEWORK_SCREEN_H__
#define __FRAMEWORK_SCREEN_H__

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/*
 * Locks used by screen configuration structures
 *
 * (l) main "lock" in framework_screen_power_config_t
 */

struct framework_screen_power_config_t;
struct framework_screen_config_t;

/*
 * Functions for working with screen power configs
 */
struct framework_screen_power_config_funcs_t {
	uint32_t(*get_brightness_low)(struct framework_screen_power_config_t *,
				      struct framework_screen_config_t *);
	uint32_t(*get_brightness_high)(struct framework_screen_power_config_t *,
				       struct framework_screen_config_t *);
	uint32_t(*get_timeout_secs)(struct framework_screen_power_config_t *,
				       struct framework_screen_config_t *);
	void(*set_brightness_low)(struct framework_screen_power_config_t *,
				  struct framework_screen_config_t *,
				  uint32_t);
	void(*set_brightness_high)(struct framework_screen_power_config_t *,
				   struct framework_screen_config_t *,
				   uint32_t);
	void(*set_timeout_secs)(struct framework_screen_power_config_t *,
				struct framework_screen_config_t *,
				uint32_t);
};

struct framework_screen_power_config_t {
	struct framework_screen_config_t *power;   /* (l) power mode configuration */
	struct framework_screen_config_t *battery; /* (l) battery mode configuration */

	/* mutex lock for accessing power config */
	struct mtx lock;

	struct framework_screen_power_config_funcs_t funcs;
};

/* Fill config structure with default values */
int framework_screen_init(struct framework_screen_power_config_t *config);

/* Get parent structure for screen config */
struct framework_screen_power_config_t *
framework_screen_config_parent(struct framework_screen_config_t *screen_config);

/* Release resources allocated through config structure */
int framework_screen_destroy(struct framework_screen_power_config_t *config);

#endif /* __FRAMEWORK_SCREEN_H__ */
