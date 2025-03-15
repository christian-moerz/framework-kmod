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

#include "framework_keyhandler.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/lock.h>

#include "framework_sysctl.h"
#include "framework_utils.h"

/* Forward declarations */
void framework_keyhandler_brightness_up(struct framework_keyhandler_t *kh);
void framework_keyhandler_brightness_down(struct framework_keyhandler_t *kh);

/*
 * links keycode to function call
 */
struct framework_keyhandler_vtable {
	uint16_t keycode;
	void(*handler_func)(struct framework_keyhandler_t *);
};

struct framework_keyhandler_vtable framework_keyhandler_vt[] = {
	{
		.keycode = 225,
		.handler_func = framework_keyhandler_brightness_up
	},
	{
		.keycode = 224,
		.handler_func = framework_keyhandler_brightness_down
	}
};

/*
 * Key handler structure
 */
struct framework_keyhandler_t {
	uint8_t flags;

	struct framework_screen_power_config_t *power_config;
};

#define FRAMEWORK_KEYHANDLER_INIT 1

MALLOC_DECLARE(M_FRAMEWORK);

/*
 * Changes brightness up or down
 *
 * if up is true, brightness goes up, otherwise down
 */
static void
framework_keyhandler_changebrightness(struct framework_keyhandler_t *kh, bool up)
{
	struct framework_screen_config_t *screen_config = NULL;
	int increment_level = 0;

	TRACE("changebrightness started\n");
	
	/* if we can't establish anything, skip */
	if (framework_util_getscreenconfig(kh->power_config, &screen_config)) {
		ERROR("cannot establish screen config\n");
		return;
	}

	/* get current increment level */
	increment_level = kh->power_config->funcs.get_increment_level(kh->power_config,
								      screen_config);

	TRACE("increment level established at %d\n", increment_level);


	TRACE("calling rel_brightness\n");
	/* then increment / decrement as necessary */
	kh->power_config->funcs.change_rel_brightness(kh->power_config,
						      screen_config,
						      up ? increment_level : -increment_level);

	TRACE("changebrightness completed\n");
}

/*
 * Changes brightness up
 */
void
framework_keyhandler_brightness_up(struct framework_keyhandler_t *kh)
{
	TRACE("brightness up call\n");
	framework_keyhandler_changebrightness(kh, true);
}

/*
 * Changes brightness down
 */
void
framework_keyhandler_brightness_down(struct framework_keyhandler_t *kh)
{
	TRACE("brightness down call\n");
	framework_keyhandler_changebrightness(kh, false);
}

/*
 * Handle a key code
 */
int
framework_keyhandler_handlekey(struct framework_keyhandler_t *kh, uint32_t key_in)
{
	if (!(FRAMEWORK_KEYHANDLER_INIT & kh->flags))
		return -1;

	TRACE("keyhandler init, key_in=%d\n", key_in);

	size_t count_max = sizeof(framework_keyhandler_vt) /
		sizeof(framework_keyhandler_vt[0]);

	for (size_t counter = 0; counter < count_max; counter++) {
		if (framework_keyhandler_vt[counter].keycode == key_in) {
			framework_keyhandler_vt[counter].handler_func(kh);
			return 0;
		}
	}

	TRACE("keyhandler no match\n");

	return -1;
}

/*
 * Initializes a new keyhandler
 */
struct framework_keyhandler_t *
framework_keyhandler_init(struct framework_screen_power_config_t *power_config)
{
	struct framework_keyhandler_t *kh = 0;

	kh = malloc(sizeof(struct framework_keyhandler_t), M_FRAMEWORK,
		    M_WAITOK | M_ZERO);

	kh->power_config = power_config;
	kh->flags = FRAMEWORK_KEYHANDLER_INIT;

	return kh;
}

/*
 * Destroy a previously allocated keyhandler
 */
void
framework_keyhandler_destroy(struct framework_keyhandler_t *kh)
{
	kh->flags |= ~FRAMEWORK_KEYHANDLER_INIT;
	free(kh, M_FRAMEWORK);
}
