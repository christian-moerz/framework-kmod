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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/malloc.h>

#include "framework_evdev.h"
#include "framework_backlight.h"
#include "framework_callout.h"
#include "framework_power.h"
#include "framework_screen.h"
#include "framework_sysctl.h"
#include "framework_state.h"
#include "framework_utils.h"

extern char cpu_model[128];

MALLOC_DEFINE(M_FRAMEWORK, "framework", "Framework module data");

/*
 * Central module data
 */
struct framework_data_t {
	/* Sysctl variables */
 	struct framework_sysctl_t sysctl;
	/* Power and brightness configuration */
	struct framework_screen_power_config_t power_config;
	/* Callout mechanism that watches input and handles dimming */
	struct framework_callout_t *callout;
	/* state structure */
	struct framework_state_t *state;
	/* module status (0 = start, 1 = init, 2 = deinit) */
	int status;
} framework_data;

/*
 * Called when module is loaded
 */
static int
framework_init(void)
{
	int error = 0;
	int undo = 0;

	bzero(&framework_data, sizeof(struct framework_data_t));

	/* Initialize state structure */
	framework_data.state = framework_state_init();

	/* Fill default values for screen config */
	error = framework_screen_init(&framework_data.power_config);
	if (0 != error) {
		DEBUG("screen init failure - error %d\n", error);
		goto framework_errorexit;
	}
	undo++; /* 1 == screen */

	/* TODO use information for checking e- and p-cores */
	DEBUG("Identified CPU model %s\n", cpu_model);

	/* Initialize power system */
	error = framework_pwr_init();
	if (0 != error) {
		ERROR("power init failure - error %d\n", error);
		goto framework_errorexit;
	}
	
	undo++; /* 2 == pwr */

	/* Initialize backlight system */
	error = framework_bl_init();
	if (0 != error) {
		ERROR("init failure of backlight - error %d\n",
		       error);
		goto framework_errorexit;
	}
	
	undo++; /* 3 == bl */

	/* Initialize sysctls */
	error = framework_sysctl_init(&framework_data.sysctl,
				      &framework_data.power_config,
				      framework_data.state);
	if (0 != error) {
		ERROR("failed to initialize sysctls - error %d\n",
		       error);
		goto framework_errorexit;
	}
	
	undo++; /* 4 == sysctl */
	
	error = framework_evdev_init();
	
	if (0 != error) {
		ERROR("failed to initialize evdev - error %d\n",
		       error);
		goto framework_errorexit;
	}
	
	undo++; /* 5 == evdev */

	framework_data.callout = framework_callout_init(&framework_data.power_config);
	if (NULL == framework_data.callout) {
		ERROR("failed to initialize callout - error %d\n", ENXIO);
		error = (ENXIO);
		goto framework_errorexit;
	}

	framework_data.status = 1;
	
	return error;

framework_errorexit:
	switch (undo)
	{
	case 6:
	case 5:
		framework_evdev_destroy();
	case 4:
		framework_sysctl_destroy(&framework_data.sysctl);
	case 3:
		framework_bl_destroy();
	case 2:
		framework_pwr_destroy();
	case 1:
		framework_screen_destroy(&framework_data.power_config);
		break;
	default:
		ERROR("Unexpected error undo case %d!\n", undo);
	}
	framework_state_destroy(framework_data.state);
	framework_data.status = 2;
	
	return error;
}

/*
 * Called when module is unloaded
 */
static int
framework_destroy(void)
{
	if (framework_data.status != 1) {
		ERROR("status %d prohibits cleanup\n",
		       framework_data.status);
		return 0;
	}
	
	/* Stop and destroy callout system */
	framework_callout_destroy(framework_data.callout);
	
	/* Stop and destroy event thread */
	framework_evdev_destroy();
	
	/* Destroy sysctls */
	framework_sysctl_destroy(&framework_data.sysctl);

	/* Destroy backlight system */
	framework_bl_destroy();

	/* Destroy power system */
	framework_pwr_destroy();

	/* Destroy screen config structure */
	framework_screen_destroy(&framework_data.power_config);

	/* Destroy state structure */
	framework_state_destroy(framework_data.state);
	
	return 0;
}

static int
framework_modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		DEBUG("MOD_LOAD\n");
		error = framework_init();
		break;
	case MOD_UNLOAD:
		DEBUG("MOD_UNLOAD\n");
		error = framework_destroy();
		break;
	default:
		DEBUG("%d\n", event);
		error = EOPNOTSUPP;
		break;
	}

	DEBUG("modevent returning %d\n", error);

	return error;
}

static moduledata_t framework_mod = {
	"framework",
	framework_modevent,
	NULL
};

DECLARE_MODULE(framework, framework_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
