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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/callout.h>
#include <sys/time.h>

#include <machine/resource.h>
#include <machine/bus.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpiio.h>

#include "framework_power.h"
#include "framework_utils.h"

static struct framework_power_t {
	enum framework_power_type_t power_state;
	/* data structure for querying ACPI power data */
	struct acpi_softc *sc;
	/* Battery device pointer */
	device_t batt_dev;
	/* Battery model name */
	char model[ACPI_CMBAT_MAXSTRLEN];
	/* Battery data */
	struct acpi_battinfo battinfo;
	/* structure lock */
	struct mtx lock;
	/* when battery info was last loaded */
	time_t last_update;
} framework_power;

#define FRAMEWORK_POWER_LOCK() mtx_lock(&framework_power.lock)
#define FRAMEWORK_POWER_UNLOCK() mtx_unlock(&framework_power.lock)

#define FRAMEWORK_POWER_CACHETIME 5

/*
 * Load battery model information from ACPI data
 */
static int
framework_pwr_loadbattmodel(void)
{
	struct acpi_bix bix = {0};
	int error = ACPI_BATT_GET_INFO(framework_power.batt_dev, &bix, sizeof(struct acpi_bix));

	if (0 != error)
		return error;

	FRAMEWORK_POWER_LOCK();
	strncpy(framework_power.model, bix.model, 7);
	FRAMEWORK_POWER_UNLOCK();
	
	return 0;
}

/*
 * Loads battery info from ACPI data
 */
static int
framework_pwr_loadbattinfo(void)
{
	int error = 0;
	/* uint8_t exit_condition = 0; */
	struct acpi_battinfo local_battinfo = {0};

	/* FRAMEWORK_POWER_LOCK();
	exit_condition = ((time_uptime - framework_power.last_update) <
			  FRAMEWORK_POWER_CACHETIME);
			  FRAMEWORK_POWER_UNLOCK();

	if (exit_condition)
		return 0; */

	DEBUG("querying battery info\n");
	error = acpi_battery_get_battinfo(NULL,
					  &local_battinfo);
	DEBUG("battery query completed with code %d\n", error);
	
	FRAMEWORK_POWER_LOCK();
	memcpy(&framework_power.battinfo, &local_battinfo, sizeof(struct acpi_battinfo));
	error = 0;

	switch (framework_power.battinfo.state) {
	case 0:
		/* unexpected value, probably on charger but not charging */
		TRACE("power got PWR-0 mode\n");
		framework_power.power_state = PWR;
		break;
	case ACPI_BATT_STAT_CRITICAL:
		/* assume discharging */
	case ACPI_BATT_STAT_DISCHARG:
		TRACE("power got BAT mode\n");
		framework_power.power_state = BAT;
		break;
	case ACPI_BATT_STAT_CHARGING:
		TRACE("power got PWR mode\n");
		framework_power.power_state = PWR;
		break;
	default:
		ERROR("Unidentified battery state %d\n",
		      framework_power.battinfo.state);
		return (EDOM);
	}

	/* update cache time */
	framework_power.last_update = time_uptime;
	FRAMEWORK_POWER_UNLOCK();

	DEBUG("completed power function with error code %d\n", error);

	return error;
}

/*
 * Initialize power system
 */
int
framework_pwr_init(void)
{
	bzero(&framework_power, sizeof(struct framework_power_t));

	mtx_init(&framework_power.lock, "framework_power", NULL, MTX_DEF);

	devclass_t batt_dc = 0;

	framework_power.sc = framework_util_lookupcdev_drv1("acpi");
	if (NULL == framework_power.sc) {
		ERROR("failed to find acpi device node\n");
		return (ENXIO);
	}

	batt_dc = devclass_find("battery");
	if (NULL == batt_dc) {
		ERROR("battery device class not found\n");
		return (ENXIO);
	}

	framework_power.batt_dev = devclass_get_device(batt_dc, 0);
	if (NULL == framework_power.batt_dev) {
		ERROR("battery device not found\n");
		return (ENXIO);
	}

	if (0 != framework_pwr_loadbattmodel()) {
		ERROR("failed to load battery model\n");
		return (ENXIO);
	}

	if (0 != framework_pwr_loadbattinfo()) {
		ERROR("failed to load battery info\n");
		return (ENXIO);
	}

	/* if (0 != strncmp("Framewo", framework_power.model, 7)) {
		printf("framework: Unsupported system\n");
		return (ENODEV);
		} */
	
	return 0;
}

/*
 * Get current power state
 */
enum framework_power_type_t
framework_pwr_getpowermode(void)
{
	if (0 != framework_pwr_loadbattinfo())
	  return IVL;

	enum framework_power_type_t result = 0;

	FRAMEWORK_POWER_LOCK();
	result = framework_power.power_state;
	FRAMEWORK_POWER_UNLOCK();

	return result;
}

/*
 * Destroy power system
 */
int
framework_pwr_destroy(void)
{
	mtx_destroy(&framework_power.lock);
	return 0;
}
