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

#ifndef __FRAMEWORK_EVDEV_THREAD_H__
#define __FRAMEWORK_EVDEV_THREAD_H__

#include <sys/kthread.h>
#include <sys/unistd.h>

#include <dev/evdev/evdev_private.h>

struct framework_evdev_thread_t;

/* callback method on input event */
typedef void(*framework_evdev_thread_cbfunc)(void *, uint16_t *);

/* Initialize event thread */
struct framework_evdev_thread_t *framework_evthread_init(size_t, struct evdev_dev *, void *);

/* Register as evdev client */
int framework_evthread_registerclient(struct framework_evdev_thread_t *ethread,
				      struct evdev_dev *dev);

/* Set callback */
void framework_evthread_setcb(struct framework_evdev_thread_t *ethread,
			      framework_evdev_thread_cbfunc cbfunc);

/* Unregister as evdev client */
int framework_evthread_unregisterclient(struct framework_evdev_thread_t *ethread,
					struct evdev_dev *dev);

/* Destroy event thread */
int framework_evthread_destroy(struct framework_evdev_thread_t *ethread);

#endif /* __FRAMEWORK_EVDEV_THREAD_H__ */
