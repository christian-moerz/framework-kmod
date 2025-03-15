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

#include "framework_power.h"
#include "framework_screen.h"

#define FRAMEWORK_SCREEN_LOCK(x) mtx_lock(&(x)->lock);
#define FRAMEWORK_SCREEN_UNLOCK(x) mtx_unlock(&(x)->lock);

#define FRAMEWORK_SCREEN_GETTER(type_size, config_name)	\
	static type_size \
	framework_screen_get ## config_name (struct framework_screen_power_config_t *config, \
					     struct framework_screen_config_t *screen_config) \
	{								\
		type_size result = 0;					\
									\
		FRAMEWORK_SCREEN_LOCK(config);				\
		result = screen_config->config_name;			\
		FRAMEWORK_SCREEN_UNLOCK(config);			\
									\
		return result;						\
	}
#define FRAMEWORK_SCREEN_SETTER(type_size, config_name)	\
	static void \
	framework_screen_set ## config_name (struct framework_screen_power_config_t *config, \
					     struct framework_screen_config_t *screen_config, \
					     type_size new_value)	\
	{								\
		FRAMEWORK_SCREEN_LOCK(config);				\
		screen_config->config_name = new_value;			\
		FRAMEWORK_SCREEN_UNLOCK(config);			\
	}
#define FRAMEWORK_SCREEN_SETGET(type_size, config_name)	\
	FRAMEWORK_SCREEN_GETTER(type_size, config_name) \
		FRAMEWORK_SCREEN_SETTER(type_size, config_name)


/*
 * Screen settings 
 */
struct framework_screen_config_t {
	uint32_t brightness_low;  /* (l) Dimmed brightness level */
	uint32_t brightness_high; /* (l) High/on brightness level */

	/*
	 * Duration of inactivity - timeout after which we switch from
	 * brightness_high to brightness_low
	 */
	uint32_t timeout_secs;

	/*
	 * The number at which we increment or decrement brightness levels */
	uint8_t increment_level;

	/* back pointer to parent structure */
	struct framework_screen_power_config_t *parent;
};

/* Module internal data */
struct framework_screen_data_t {
	struct framework_screen_config_t power;
	struct framework_screen_config_t battery;
} framework_screen_data;

FRAMEWORK_SCREEN_SETGET(uint32_t, brightness_low);
FRAMEWORK_SCREEN_SETGET(uint32_t, brightness_high);
FRAMEWORK_SCREEN_SETGET(uint32_t, timeout_secs);
FRAMEWORK_SCREEN_GETTER(uint8_t, increment_level);

/*
 * Get parent of screen config
 */
struct framework_screen_power_config_t *
framework_screen_config_parent(struct framework_screen_config_t *screen_config)
{
	return screen_config->parent;
}

/*
 * Change the upper brightness level
 */
static int
framework_screen_config_changebrightness(struct framework_screen_power_config_t *config,
					 struct framework_screen_config_t *screen_config,
					 int relative)
{
	uint32_t brightness = 0;

	FRAMEWORK_SCREEN_LOCK(config);
	brightness = screen_config->brightness_high;
	FRAMEWORK_SCREEN_UNLOCK(config);
	
	if (relative < 0) {
		/* we want to reduce the screen brightness */

		/* we are already at the "bottom" */
		if (0 == brightness)
			return -1;
		
		if ((0 - relative) > brightness) {
			/* we would reduce below zero, shich we cannot */
			FRAMEWORK_SCREEN_LOCK(config);
			screen_config->brightness_high = 0;
			FRAMEWORK_SCREEN_UNLOCK(config);
			return -1;
		}

		FRAMEWORK_SCREEN_LOCK(config);
		screen_config->brightness_high += relative;
		FRAMEWORK_SCREEN_UNLOCK(config);
		return 0;
	} else {
		/* we want to increase the brightness */

		/* we are already at the "top" */
		if (100 == brightness)
			return -1;

		if ((brightness + relative) > 100) {
			/* cap at a 100 */
			FRAMEWORK_SCREEN_LOCK(config);
			screen_config->brightness_high = 100;
			FRAMEWORK_SCREEN_UNLOCK(config);
			return -1;
		}

		FRAMEWORK_SCREEN_LOCK(config);
		screen_config->brightness_high += relative;
		FRAMEWORK_SCREEN_UNLOCK(config);
		return 0;
	}

	return -1;
}

/*
 * Set up screen config structure with default values
 */
int
framework_screen_init(struct framework_screen_power_config_t *config)
{
	framework_screen_data.power.timeout_secs = 10;
	framework_screen_data.power.brightness_low = 30;
	framework_screen_data.power.brightness_high = 100;
	framework_screen_data.power.increment_level = 10;

	framework_screen_data.battery.timeout_secs = 10;
	framework_screen_data.battery.brightness_low = 3;
	framework_screen_data.battery.brightness_high = 40;
	framework_screen_data.battery.increment_level = 10;

	config->funcs.get_brightness_low = framework_screen_getbrightness_low;
	config->funcs.set_brightness_low = framework_screen_setbrightness_low;
	config->funcs.get_brightness_high = framework_screen_getbrightness_high;
	config->funcs.set_brightness_high = framework_screen_setbrightness_high;
	config->funcs.get_timeout_secs = framework_screen_gettimeout_secs;
	config->funcs.set_timeout_secs = framework_screen_settimeout_secs;
	config->funcs.get_increment_level = framework_screen_getincrement_level;
	config->funcs.change_rel_brightness = framework_screen_config_changebrightness;

	mtx_init(&config->lock, "framework_screen", NULL, MTX_DEF);
	FRAMEWORK_SCREEN_LOCK(config);
	config->power = &framework_screen_data.power;
	config->battery = &framework_screen_data.battery;
	config->power->parent = config;
	config->battery->parent = config;
	FRAMEWORK_SCREEN_UNLOCK(config);

	return 0;
}

/*
 * Release previously allocated data in config
 */
int
framework_screen_destroy(struct framework_screen_power_config_t *config)
{
	mtx_destroy(&config->lock);
	
	return 0;
}
