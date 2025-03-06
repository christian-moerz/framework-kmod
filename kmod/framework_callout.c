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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/callout.h>

#include "framework_backlight.h"
#include "framework_evdev.h"
#include "framework_callout.h"
#include "framework_power.h"
#include "framework_screen.h"
#include "framework_sysctl.h"
#include "framework_utils.h"

struct framework_callout_t {
	/* Back-pointer to screen power configuration */
	struct framework_screen_power_config_t *power_config;

	/* (r) Cache currently expected level */
	enum framework_callout_brightmode_t current_level;

	int expect_next_callout;          /* (l) tick at which to expect next callout */

	int active;                       /* active flag */
	
	struct mtx lock;                  /* l - structure and callout lock */
	struct rwlock rwlock;             /* r - rwlock for internal vars */
};

#define FRAMEWORK_CALLOUT_LOCK(x) mtx_lock(&(x)->lock)
#define FRAMEWORK_CALLOUT_UNLOCK(x) mtx_unlock(&(x)->lock)
#define FRAMEWORK_CALLOUT_RLOCK(x) rw_rlock(&(x)->rwlock)
#define FRAMEWORK_CALLOUT_WLOCK(x) rw_wlock(&(x)->rwlock)
#define FRAMEWORK_CALLOUT_RUNLOCK(x) rw_runlock(&(x)->rwlock)
#define FRAMEWORK_CALLOUT_WUNLOCK(x) rw_wunlock(&(x)->rwlock)
#define FRAMEWORK_CALLOUT_LOCK_ASSERT(x) mtx_assert(&(x)->lock, MA_OWNED)
#define FRAMEWORK_CALLOUT_MINTIMEOUT 5

MALLOC_DECLARE(M_FRAMEWORK);

static uint8_t framework_callout_drop = 1;

/*
 * Retrieve currently valid brightness level
 */
static uint32_t
framework_callout_getbrightnessfor(struct framework_callout_t *co)
{
	struct framework_screen_config_t *screen_config = NULL;
	uint32_t brightness = 0;

	if (NULL == co->power_config) {
		ERROR("power_config invalid!\n");
	}
	if (NULL == co->power_config->funcs.get_brightness_low) {
		ERROR("power_config function get_brightness_low invalid\n");
	}
	if (NULL == co->power_config->funcs.get_brightness_high) {
		ERROR("power_config function get_brightness_high invalid\n");
	}
	
	switch (framework_pwr_getpowermode()) {
	case BAT:
		screen_config = co->power_config->battery;
		break;
	case PWR:
		screen_config = co->power_config->power;
		break;
	case IVL:
		ERROR("callout received invalid power mode\n");
		return -1;
	}

	if (NULL == screen_config) {
		ERROR("invalid screen_config\n");
	}

	FRAMEWORK_CALLOUT_RLOCK(co);
	switch (co->current_level) {
	case DIM:
		brightness = co->power_config->funcs.get_brightness_low(co->power_config,
									 screen_config);
		break;
	case HIGH:
		brightness = co->power_config->funcs.get_brightness_high(co->power_config,
									  screen_config);
		break;
	}
	FRAMEWORK_CALLOUT_RUNLOCK(co);

	return brightness;
}

/*
 * Get current timeout for dimming
 */
static uint32_t
framework_callout_getcurrenttimeout(struct framework_callout_t *co)
{
	struct framework_screen_config_t *screen_config = NULL;
	int state = framework_pwr_getpowermode();
	uint32_t timeout_secs = 0;

	switch (state) {
	case BAT:
		TRACE("callout getpowermode returned BAT\n");
		screen_config = co->power_config->battery;
		break;
	case PWR:
		TRACE("callout getpowermode returned PWR\n");
		screen_config = co->power_config->power;
		break;
	case IVL:
		TRACE("callout timeout check got INVALID battery state %d\n",
		      state);
		return 0;
	}

	if (!co->power_config) {
		ERROR("callout poewr_config invalid\n");
		return 0;
	}

	if (!co->power_config->funcs.get_timeout_secs) {
		ERROR("callout poewr_config func get_timeout_secs invalid\n");
		return 0;
	}
	
	/* get number of seconds for timeout */
	timeout_secs = co->power_config->funcs.get_timeout_secs(co->power_config,
								screen_config);

	DEBUG("framework: callout got %u timeout seconds for current power mode\n",
	      timeout_secs);

	return timeout_secs;
}

/*
 * Called when input interrupt is received
 */
static void
framework_callout_inputintr(void *ctx)
{
	struct framework_callout_t *co = ctx;
	uint32_t brightness = 0;

	TRACE("callout inputintr begin\n");

	/* no longer accept any further input signals */
	if (framework_callout_drop) {
		TRACE("callout dropping because global flag set\n");
		return;
	}

	TRACE("callout locking\n");
	FRAMEWORK_CALLOUT_WLOCK(co);
	/* Reset to high now */
	co->current_level = HIGH;
	FRAMEWORK_CALLOUT_WUNLOCK(co);
	brightness = framework_callout_getbrightnessfor(co);
	TRACE("callout unlocked\n");

	framework_bl_setbrightness(brightness);

	TRACE("callout intr end\n");
}

/*
 * Calculate tick count from seconds
 */
static uint32_t
framework_callout_sec2tick(uint32_t seconds)
{
	struct timeval tv = {0};

	tv.tv_sec = seconds;

	return tvtohz(&tv);
}

/*
 * Kernel thread running dim check at expected intervals
 */
static void
framework_callout_thread(void *ptr)
{
	struct framework_callout_t *co = ptr;
	uint32_t current_timeout = 0;
	time_t last_input = 0;
	uint32_t elapsed_time = 0;
	uint32_t next_seconds = 0;
	uint32_t next_wait = 0;
	uint32_t brightness = 0;

	TRACE("callout thread start\n");

	/* Wire up interrupt */
	framework_evdev_setintrfunc(framework_callout_inputintr, co);
	framework_callout_drop = 0;
	
	FRAMEWORK_CALLOUT_LOCK(co);
	while (co->active) {
		/* get current timeout */
		FRAMEWORK_CALLOUT_UNLOCK(co);
		current_timeout = framework_callout_getcurrenttimeout(co);
		FRAMEWORK_CALLOUT_LOCK(co);
		
		TRACE("callout thread timeout at %d seconds\n",
		       current_timeout);

		if (0 == current_timeout) {
			/* invalid timeout */
			ERROR("invalid timeout value - exiting\n");
			co->active = 0;
			break;
		}

		/* get last input time */
		FRAMEWORK_CALLOUT_UNLOCK(co);
		last_input = framework_evdev_getlastinput();
		FRAMEWORK_CALLOUT_LOCK(co);
		
		/* prevent overflow */
		if (last_input > time_uptime)
			last_input = time_uptime;
		
		/* calculate elapsed time since last input */
		elapsed_time = (time_uptime - last_input);
		TRACE("callout thread last input at %d seconds ago\n",
		       elapsed_time);

		/* call dimcheck */
		if (elapsed_time >= current_timeout) {
			/* dim if we exeeded timeout */
			FRAMEWORK_CALLOUT_WLOCK(co);
			co->current_level = DIM;
			FRAMEWORK_CALLOUT_WUNLOCK(co);
		} /* else {
			co->current_level = HIGH;
			}*/

		FRAMEWORK_CALLOUT_UNLOCK(co);
		brightness = framework_callout_getbrightnessfor(co);
		framework_bl_setbrightness(brightness);
		FRAMEWORK_CALLOUT_LOCK(co);

		/* calculate next wait duration */
		next_seconds = (elapsed_time < current_timeout) ?
			(current_timeout - elapsed_time) : current_timeout;
		next_wait = framework_callout_sec2tick(next_seconds);
		TRACE("callout thread will wake up again in %d ticks (%d secs)\n",
		      next_wait, next_seconds);
		co->expect_next_callout = tick + next_wait;
		
		msleep(co, &co->lock, 0, "sigwait", next_wait);
	}
	FRAMEWORK_CALLOUT_UNLOCK(co);

	wakeup(co);
	TRACE("callout thread stopped\n");

	kthread_exit();
}

/*
 * Initialize a new callout handler
 */
struct framework_callout_t *
framework_callout_init(struct framework_screen_power_config_t *power_config)
 {
	struct framework_callout_t *co = NULL;
	uint32_t brightness = 0;

	co = malloc(sizeof(struct framework_callout_t),
		    M_FRAMEWORK, M_WAITOK | M_ZERO);

	co->power_config = power_config;
	co->active = 1;

	mtx_init(&co->lock, "framework_callout", NULL, MTX_DEF);
	rw_init(&co->rwlock, "framework_callout_rw");

	/* set to expected high value */
	co->current_level = HIGH;
	
	brightness = framework_callout_getbrightnessfor(co);
	framework_bl_setbrightness(brightness);

	/* Schedule initial callout */
	int error = kthread_add(framework_callout_thread, co, NULL,
				NULL, 0, 0,
				"framework_callout_thread");
	if (0 != error) {
		ERROR("failed to start callout thread with error %d\n", error);
	}
	
	return co;
}

/*
 * Destroys callout handler
 */
void
framework_callout_destroy(struct framework_callout_t *co)
{
	/* Clear interrupt callback */
	framework_evdev_setintrfunc(NULL, NULL);
	framework_callout_drop = 1;

	if (NULL == co)
		return;

	FRAMEWORK_CALLOUT_LOCK(co);
	if (co->active) {
		co->active = 0;
		wakeup(co);
		/* wait for thread to finish */
		msleep(co, &co->lock, 0, "sigwait", 0);
	}
	FRAMEWORK_CALLOUT_UNLOCK(co);

	rw_destroy(&co->rwlock);
	mtx_destroy(&co->lock);
	free(co, M_FRAMEWORK);
}
