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

#include <dev/backlight/backlight.h>

#include "backlight_if.h"

#include "framework_backlight.h"
#include "framework_utils.h"

static struct framework_backlight_t {
	struct backlight_softc *sc;
	struct backlight_props props;
} framework_backlight;

/* internal structure definition, borrowed from dev/backlight */
struct backlight_softc {
	struct cdev *cdev;
	struct cdev *alias;
	int unit;
	device_t dev;
	uint32_t cached_brightness;
};

/*
 * load properties from backlight
 */
static int
framework_bl_loadprops(void)
{
	return BACKLIGHT_GET_STATUS(framework_backlight.sc->dev,
				    &framework_backlight.props);
}

/*
 * Initialize backlight data structure
 */
int
framework_bl_init(void)
{
	bzero(&framework_backlight, sizeof(struct framework_backlight_t));

	framework_backlight.sc =
		framework_util_lookupcdev_drv1("backlight/backlight0");
	if (NULL == framework_backlight.sc)
		return (ENXIO);

	/* load settings into props at least once */
	return framework_bl_loadprops();
}

/*
 * Get current brightness level
 */
uint32_t
framework_bl_getbrightness(void)
{
	if (NULL == framework_backlight.sc) {
		ERROR("backlight not initialized.\n");
		return 0;
	}

	if (0 == framework_bl_loadprops())
		return framework_backlight.props.brightness;

	ERROR("failed to read backlight data.\n");

	return 0;
}

/*
 * Set new brightness level
 */
int
framework_bl_setbrightness(uint32_t brightness)
{
	int error = 0;
	uint32_t current_level = 0;
	
	if (NULL == framework_backlight.sc)
		return (ENXIO);

	current_level = framework_bl_getbrightness();
	if (brightness == current_level)
		return 0;

	framework_backlight.props.brightness = brightness;
	error = BACKLIGHT_UPDATE_STATUS(framework_backlight.sc->dev,
					&framework_backlight.props);
	if (0 == error)
		framework_backlight.sc->cached_brightness = brightness;

	return error;
}

/*
 * Free any data associated with backlight
 */
int
framework_bl_destroy(void)
{
	return 0;
}
