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

#ifndef __FRAMEWORK_EVDEV_H__
#define __FRAMEWORK_EVDEV_H__

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kthread.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/queue.h>

#include "framework_evdev_thread.h"

/*
 * Callback prototype for interrupt function
 */
typedef void(*framework_evdev_intrfunc)(void *);

/*
 * A bound evdev device
 */
struct framework_evdev_binding_t {
	struct framework_evdev_thread_t *listener_thread;
	struct evdev_dev *evdev_device;

	LIST_ENTRY(framework_evdev_binding_t) entries;
};

struct framework_evdev_t;

/* Initialize evdev system */
int framework_evdev_init(void);

/* Get boot time seconds when last input occurred */
time_t framework_evdev_getlastinput(void);

/* Set interrupt callback function */
void framework_evdev_setintrfunc(framework_evdev_intrfunc cbfunc, void *);

/* Destroy evdev system */
int framework_evdev_destroy(void);

#endif /* __FRAMEWORK_EVDEV_H__ */
