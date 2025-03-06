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

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include "framework_backlight.h"
#include "framework_power.h"
#include "framework_screen.h"
#include "framework_sysctl.h"

static char *FRAMEWORK_POWER_PWR = "PWR";
static char *FRAMEWORK_POWER_BAT = "BAT";
static char *FRAMEWORK_POWER_IVL = "INVALID";

static struct framework_sysctl_t *sysctl_cache = 0;

#define FRAMEWORK_SYSCTL_NODE(parent_node, name, description)		\
	SYSCTL_ADD_NODE(&fsp->framework_sysctl_ctx,			\
			SYSCTL_CHILDREN(fsp->oid_framework_ ## parent_node), \
			OID_AUTO,					\
			name,						\
			CTLFLAG_RD | CTLFLAG_MPSAFE,			\
			0,						\
			description);

#define FRAMEWORK_SYSCTL_SCREENCONF_HANDLER(var_name, max_value)	\
	static int \
	framework_sysctl_screen_config_ ## var_name (SYSCTL_HANDLER_ARGS) \
	{ \
		int error = 0;					\
								\
		struct framework_screen_config_t *screen_config = arg1; \
		struct framework_screen_power_config_t *config =	\
			framework_screen_config_parent(screen_config);	\
									\
		uint32_t orig_value = config->funcs.get_## var_name (config, \
								     screen_config); \
		uint32_t value = orig_value;				\
									\
		error = sysctl_handle_32(oidp, &value, 0, req);		\
									\
		if (value != orig_value) {				\
			if (0 != max_value) {				\
				if (value > max_value)			\
					return error;			\
			}						\
			config->funcs.set_ ## var_name (config, screen_config, value); \
		}							\
									\
		return error;						\
	}

#define FRAMEWORK_SYSCTL_SCREENCONF_NODES(var_name, description)	\
	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,			\
			SYSCTL_CHILDREN(fsp->oid_framework_screen_battery_tree), \
			OID_AUTO, #var_name,				\
			CTLTYPE_U32 | CTLFLAG_RW | CTLFLAG_MPSAFE,	\
			power_config->battery, 0,			\
			framework_sysctl_screen_config_ ## var_name, "IU", \
			description);					\
									\
	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,			\
			SYSCTL_CHILDREN(fsp->oid_framework_screen_power_tree), \
			OID_AUTO, #var_name,				\
			CTLTYPE_U32 | CTLFLAG_RW | CTLFLAG_MPSAFE,	\
			power_config->power, 0,				\
			framework_sysctl_screen_config_ ## var_name, "IU", \
			description);

/*
 * Called to process brightness level
 *
 * Parameters:
 * - struct sysctl_oid *oidp
 * - void *arg1,
 * - intmax_t arg2,
 * - struct syctl_req *req
 */
static int
framework_sysctl_screen_brightness(SYSCTL_HANDLER_ARGS)
{
	int error = 0;
	uint32_t value = framework_bl_getbrightness();

	error = sysctl_handle_32(oidp, &value, 0, req);
	
	return error;
}

FRAMEWORK_SYSCTL_SCREENCONF_HANDLER(brightness_low, 100);
FRAMEWORK_SYSCTL_SCREENCONF_HANDLER(brightness_high, 100);
FRAMEWORK_SYSCTL_SCREENCONF_HANDLER(timeout_secs, 0);

/*
 * Called to process power source sysctl
 */
static int
framework_sysctl_power_source(SYSCTL_HANDLER_ARGS)
{
	int error = 0;

	enum framework_power_type_t pwr_type = framework_pwr_getpowermode();
	void *ptr = 0;

	switch (pwr_type) {
	case BAT:
		ptr = FRAMEWORK_POWER_BAT;
		break;
	case PWR:
		ptr = FRAMEWORK_POWER_PWR;
		break;
	default:
		ptr = FRAMEWORK_POWER_IVL;
	}

	error = sysctl_handle_string(oidp, ptr, 0, req);

	return error;
}

/*
 * Get current debug level
 */
uint8_t
framework_sysctl_debuglevel(void)
{
	uint8_t result = 0;

	if (NULL == sysctl_cache)
		return 2;
	
	FRAMEWORK_SYSCTL_LOCK(sysctl_cache);
	result = sysctl_cache->debug;
	FRAMEWORK_SYSCTL_UNLOCK(sysctl_cache);
	return result;		
}

/*
 * Called to process debug sysctl
 */
static int
framework_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	struct framework_sysctl_t *sys = arg1;

	FRAMEWORK_SYSCTL_LOCK(sys);
	uint32_t value = sys->debug;
	FRAMEWORK_SYSCTL_UNLOCK(sys);

	int error = sysctl_handle_32(oidp, &value, 0, req);

	if (value > 255)
		value = 255;
	if (value != sys->debug) {
		FRAMEWORK_SYSCTL_LOCK(sys);
		sys->debug = value;
		FRAMEWORK_SYSCTL_UNLOCK(sys);
	}

	return error;
}

/*
 * Called to process dim blocker
 */
static int
framework_sysctl_dimblock(SYSCTL_HANDLER_ARGS)
{
	struct framework_state_t *state = arg1;

	uint32_t counter = framework_state_getdimcount(state);
	uint32_t value = counter;

	int error = sysctl_handle_32(oidp, &value, 0, req);

	if (value != counter) {
		if (value > 0) {
			/* increment counter */
			framework_state_incdimcount(state);
		} else {
			/* decrement counter */
			framework_state_decdimcount(state);
		}
	}

	return error;
}

/*
 * Initialize framework sysctl structure
 */
int
framework_sysctl_init(struct framework_sysctl_t *fsp,
		      struct framework_screen_power_config_t *power_config,
		      struct framework_state_t *state)
{
	/* Store power config reference */
	fsp->power_config = power_config;

	/* Store state reference */
	fsp->state = state;

	/* Default to disabled debug mode */
	fsp->debug = 0;

	mtx_init(&fsp->lock, "framework_sysctl", 0, MTX_DEF);
	
	/* create sysctl context */
	sysctl_ctx_init(&fsp->framework_sysctl_ctx);

	fsp->oid_framework_tree = SYSCTL_ADD_NODE(&fsp->framework_sysctl_ctx,
						  SYSCTL_STATIC_CHILDREN(_hw),
						  OID_AUTO,
						  "framework",
						  CTLFLAG_RD | CTLFLAG_MPSAFE,
						  0,
						  "Frame.work hardware");

	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,
			SYSCTL_CHILDREN(fsp->oid_framework_tree),
			OID_AUTO, "debug",
			CTLTYPE_U32 | CTLFLAG_RW | CTLFLAG_MPSAFE,
			fsp, 0,
			framework_sysctl_debug, "IU",
			"Enable verbose logging");
	
	fsp->oid_framework_screen_tree =
		FRAMEWORK_SYSCTL_NODE(tree, "screen",
				"Frame.work screen config");

	fsp->oid_framework_power_tree =
		FRAMEWORK_SYSCTL_NODE(tree, "power",
				"Frame.work battery and power");

	fsp->oid_framework_screen_power_tree =
		FRAMEWORK_SYSCTL_NODE(screen_tree, "power",
				      "Settings when on power");
	
	fsp->oid_framework_screen_battery_tree =
		FRAMEWORK_SYSCTL_NODE(screen_tree, "battery",
				      "Settings when on battery");

	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,
			SYSCTL_CHILDREN(fsp->oid_framework_screen_tree),
			OID_AUTO, "dimblock",
			CTLTYPE_U32 | CTLFLAG_RW | CTLFLAG_MPSAFE,
			state, 0,
			framework_sysctl_dimblock, "IU",
			"Block screen from dimming while >0");
	
	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,
			SYSCTL_CHILDREN(fsp->oid_framework_screen_tree),
			OID_AUTO, "brightness_current",
			CTLTYPE_U32 | CTLFLAG_RD | CTLFLAG_MPSAFE,
			NULL, 0,
			framework_sysctl_screen_brightness, "IU",
			"Current screen brightness level");

	SYSCTL_ADD_PROC(&fsp->framework_sysctl_ctx,
			SYSCTL_CHILDREN(fsp->oid_framework_power_tree),
			OID_AUTO, "powermode",
			CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
			NULL, 0,
			framework_sysctl_power_source, "A",
			"Power source");

	FRAMEWORK_SYSCTL_SCREENCONF_NODES(brightness_low, "Lower brightness threshold");
	FRAMEWORK_SYSCTL_SCREENCONF_NODES(brightness_high, "Upper brightness threshold");
	FRAMEWORK_SYSCTL_SCREENCONF_NODES(timeout_secs, "Timeout for switch from high to low");

	sysctl_cache = fsp;
	
	return 0;
}

/*
 * Remove framework sysctl structure
 */
int
framework_sysctl_destroy(struct framework_sysctl_t *fsp)
{
	if (NULL == fsp)
		return 0;

	mtx_destroy(&fsp->lock);
  
	int error = sysctl_ctx_free(&fsp->framework_sysctl_ctx);
	
	fsp->power_config = NULL;
	
	return error;
}
