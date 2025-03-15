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
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/time.h>

#include "framework_evdev.h"
#include "framework_sysctl.h"
#include "framework_utils.h"

static const char *framework_evdev_devnames[] = {
	"TouchPad",
	"Mouse",
	"hcons0",
	"hmt0",
	"hms0",
	"sysmouse",
	"kbdmux",
	"psm",
	"atkbd",
	NULL
};

/*
 * evdev connector
 *
 * Attaches to an evdev device and adds a listener
 * client to receive updates from input signals
 */
static struct framework_evdev_t {
	LIST_HEAD(, framework_evdev_binding_t) bindings;

	time_t last_input;               /* (l) time_uptime the last input occurred */

	framework_evdev_intrfunc cbfunc; /* (l) interrupt function */
	void *cbctx;                     /* (l) callback context */

	uint8_t active;

	struct mtx lock;                 /* l - lock mechanism */

	uint8_t state;
} framework_evdev = {0};

#define FRAMEWORK_EVDEV_LOCK(x) mtx_lock(&(x)->lock)
#define FRAMEWORK_EVDEV_UNLOCK(x) mtx_unlock(&(x)->lock)

MALLOC_DECLARE(M_FRAMEWORK);

/*
 * ATTENTION
 *
 * On every release upgrade, we need to check if this value
 * is still correct -> dev/evdev!
 */
#define	DEF_RING_REPORTS	8

time_t
framework_evdev_getlastinput(void)
{
	time_t tval = 0;

	FRAMEWORK_EVDEV_LOCK(&framework_evdev);
	tval = framework_evdev.last_input;
	FRAMEWORK_EVDEV_UNLOCK(&framework_evdev);

	return tval;
}

/*
 * Called when input is received
 */
static void
framework_evdev_oninput(void *ctx, uint16_t *keycode)
{
	struct framework_evdev_t *edata = ctx;
	framework_evdev_intrfunc local_cbfunc = NULL;
	void *local_ctx = NULL;

	if (!edata->active) {
		TRACE("evdev oninput callback while inactive\n");
		return;
	}

	TRACE("evdev oninput lock\n");
	FRAMEWORK_EVDEV_LOCK(edata);
	edata->last_input = time_uptime;
	TRACE("last input updated to %ld\n", edata->last_input);
	
	local_cbfunc = framework_evdev.cbfunc;
	local_ctx = framework_evdev.cbctx;
	FRAMEWORK_EVDEV_UNLOCK(edata);
	TRACE("evdev oninput unlock\n");

	if (local_cbfunc) {
		TRACE("calling evdev callback at %p\n", local_cbfunc);
		local_cbfunc(local_ctx, keycode);
	}
}

/*
 * Check whether device needs to be monitored
 *
 * We don't want to monitor screen lid buttons, acpi video devices
 * and similar. This may lead to various timing conflicts.
 */
static bool
framework_evdev_matchname(const char *name, bool partial)
{
	if (NULL == name)
		return false;

	const char **strptr = framework_evdev_devnames;
	size_t slen = 0;

	while (*strptr) {
		TRACE("matchname(\"%s\" =~ \"%s\"?)\n",
		       name, *strptr);
		
		slen = strlen(*strptr);
		
		if (!partial && !strncmp(*strptr, name, slen)) {
			TRACE("matchname precise OK\n");
			return true;
		}
		if (partial && strnstr(*strptr, name, slen)) {
			TRACE("matchname partial OK\n");
			return true;
		}
		
		strptr++;
	}

	TRACE("matchname NO\n");

	return false;
}

/*
 * Callback method for evdev iteration
 */
static int
framework_evdev_matchdevs(const char *name,
			  void *drv_data,
			  void *ctx)
{
	struct framework_evdev_t *edata = ctx;
	struct evdev_dev *devdata = drv_data;
	
	size_t buffer_size = devdata->ev_report_size * DEF_RING_REPORTS;

	if (!framework_evdev_matchname(devdata->ev_shortname, false)) {
		if (!framework_evdev_matchname(devdata->ev_name, true))
			return 0;
	}
	
	struct framework_evdev_binding_t *binding =
		malloc(sizeof(struct framework_evdev_binding_t),
		       M_FRAMEWORK,
		       M_WAITOK | M_ZERO);

	DEBUG("Registering for %s\n", name);
	DEBUG("ev_name = \"%s\", ev_shortname = \"%s\"\n",
	      devdata->ev_name, devdata->ev_shortname);
	DEBUG("ev_serial = \"%s\"\n", devdata->ev_serial);
	DEBUG("bustype = %d, vendor = %d, product = %d, version = %d\n",
	      devdata->ev_id.bustype,
	      devdata->ev_id.vendor,
	      devdata->ev_id.product,
	      devdata->ev_id.version);
	
	binding->evdev_device = devdata;
	binding->listener_thread = framework_evthread_init(buffer_size,
							   devdata,
							   edata);
	if (binding->listener_thread)
		framework_evthread_setcb(binding->listener_thread,
					 framework_evdev_oninput);

	FRAMEWORK_EVDEV_LOCK(edata);
	LIST_INSERT_HEAD(&edata->bindings, binding, entries);
	FRAMEWORK_EVDEV_UNLOCK(edata);

	/* After starting thread, we register as client */
	framework_evthread_registerclient(binding->listener_thread, binding->evdev_device);
	
	return 0;
}

/*
 * Initialize evdev connector
 */
int
framework_evdev_init(void)
{
	LIST_INIT(&framework_evdev.bindings);

	framework_evdev.cbfunc = NULL;
	framework_evdev.active = 1;
	
	mtx_init(&framework_evdev.lock,
		 "framework_evdev", NULL, MTX_DEF);
	
	int error = framework_util_matchcdev_drv1("input/event",
						  framework_evdev_matchdevs,
						  &framework_evdev);

	framework_evdev.state = 1;
	
	return error;
}

/*
 * Set evdev interrupt function callback pointer
 */
void
framework_evdev_setintrfunc(framework_evdev_intrfunc cbfunc, void *ctx)
{
	FRAMEWORK_EVDEV_LOCK(&framework_evdev);
	framework_evdev.cbfunc = cbfunc;
	framework_evdev.cbctx = ctx;
	FRAMEWORK_EVDEV_UNLOCK(&framework_evdev);
}

/*
 * Destroy evdev connector
 */
int
framework_evdev_destroy(void)
{
	struct framework_evdev_binding_t *binding = NULL;

	if (0 == framework_evdev.state)
		return 0;

	TRACE("evdev destroy lock\n");
	FRAMEWORK_EVDEV_LOCK(&framework_evdev);
	framework_evdev.cbfunc = NULL;
	framework_evdev.active = 0;

	while (!LIST_EMPTY(&framework_evdev.bindings)) {
		binding = LIST_FIRST(&framework_evdev.bindings);
		LIST_REMOVE(binding, entries);
		TRACE("evdev destroying binding %p\n", binding);

		/* Unregister client before destroying the binding */
		if (binding->listener_thread) {
			TRACE("evdev unlocking, destroying thread\n");
			FRAMEWORK_EVDEV_UNLOCK(&framework_evdev);
			framework_evthread_unregisterclient(binding->listener_thread, binding->evdev_device);
			framework_evthread_destroy(binding->listener_thread);
			binding->listener_thread = NULL;

			TRACE("evdev relocking\n");
			FRAMEWORK_EVDEV_LOCK(&framework_evdev);
		} else {
			ERROR("evdev listener thread unavailable\n");
		}
					
		binding->evdev_device = NULL;
		free(binding, M_FRAMEWORK);
	}
	FRAMEWORK_EVDEV_UNLOCK(&framework_evdev);
	TRACE("evdev destroy unlock\n");
	
	mtx_destroy(&framework_evdev.lock);

	framework_evdev.state = 0;
	
	return 0;
}
