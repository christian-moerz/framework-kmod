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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kthread.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/conf.h>

#include "framework_evdev_thread.h"
#include "framework_utils.h"

#include <dev/evdev/evdev_private.h>
#include <fs/devfs/devfs_int.h>

/*
 * Event thread structure
 */
struct framework_evdev_thread_t {
	struct mtx session;      /* session lock */

	uint8_t active;          /* flag whether thread should remain active */

	/* evdev_client.ec_buffer_mtx used as lock mechanism for structure! */
	struct evdev_client *evdev_client;    /* client structure */

	void *ctx;

	framework_evdev_thread_cbfunc cbfunc; /* callback function */

	uint8_t flags;
};

#define FRAMEWORK_EVTHREAD_LOCK(x) mtx_lock(&x->evdev_client->ec_buffer_mtx)
#define FRAMEWORK_EVTHREAD_UNLOCK(x) mtx_unlock(&x->evdev_client->ec_buffer_mtx)
#define FRAMEWORK_EVSESSION_LOCK(x) mtx_lock(&x->session)
#define FRAMEWORK_EVSESSION_UNLOCK(x) mtx_unlock(&x->session)

#define FRAMEWORK_EVSESSION_FLAG_CLIENTREG 1
#define FRAMEWORK_EVSESSION_FLAG_KQUEUE 2
#define FRAMEWORK_EVSESSION_FLAG_THREAD 4
#define FRAMEWORK_EVSESSION_FLAG_SHUTDOWN 8 

MALLOC_DECLARE(M_FRAMEWORK);

/*
 * Clean up kqueue
 */
static void
framework_evthread_clearkqueue(struct evdev_client *client)
{
	TRACE("evdev thread clear kqueue begin\n");
	client->ec_buffer_head = client->ec_buffer_tail =
		client->ec_buffer_ready = 0;
	client->ec_clock_id = 0;
	
	if (!knlist_empty(&client->ec_selp.si_note)) {
		knlist_clear(&client->ec_selp.si_note, true);
	}
	TRACE("evdev thread clear kqueue end\n");
}

/*
 * Get flags in thread safe version
 */
static uint8_t
framework_evthread_getflags(struct framework_evdev_thread_t *ethread)
{
	uint8_t local_flags = 0;
	
	FRAMEWORK_EVSESSION_LOCK(ethread);
	local_flags = ethread->flags;
	FRAMEWORK_EVSESSION_UNLOCK(ethread);

	return local_flags;
}

/*
 * Thread event handler
 */
static void
framework_evthread_func(void *data)
{
	struct framework_evdev_thread_t *edata = data;
	uint8_t local_active = true;
	int error = 0;
	framework_evdev_thread_cbfunc local_cbfunc;

	TRACE("Started evdev thread with edata = %p.\n", edata);

	/* add THREAD flag */
	FRAMEWORK_EVSESSION_LOCK(edata);
	TRACE("evdev thread adding THREAD flag\n");
	edata->flags |= FRAMEWORK_EVSESSION_FLAG_THREAD;
	FRAMEWORK_EVSESSION_UNLOCK(edata);

	while (local_active) {
		/* Need to reset blocked to ensure we are woken up on next signal */
		FRAMEWORK_EVTHREAD_LOCK(edata);
		if (!edata->active) {
			/* We got exit */
			FRAMEWORK_EVTHREAD_UNLOCK(edata);
			ERROR("evdev thread jumping the shark\n");
			break;
		}
		
		TRACE("evdev thread mtx sleep begin\n");
		edata->evdev_client->ec_blocked = true;
		error = msleep(edata->evdev_client,
			       &edata->evdev_client->ec_buffer_mtx,
			       0, "sigwait", 0);
		TRACE("evdev thread mtx sleep awoken\n");
		
		if (0 != error) {
			ERROR("failed to mutex sleep in thread");
		} else {
			/* reset buffer position and clear kqueue */
			framework_evthread_clearkqueue(edata->evdev_client);
			
			TRACE("evdev thread locking session\n");
			FRAMEWORK_EVSESSION_LOCK(edata);
			local_cbfunc = edata->cbfunc;
			FRAMEWORK_EVSESSION_UNLOCK(edata);
			TRACE("evdev thread unlocking session\n");
			
			/* moved back into lock */
			if (local_cbfunc) {
				/* direct to callback function */
				TRACE("evdev thread callback begin\n");
				FRAMEWORK_EVTHREAD_UNLOCK(edata);
				edata->cbfunc(edata->ctx);
				FRAMEWORK_EVTHREAD_LOCK(edata);
				TRACE("evdev thread callback end\n");
			}
		}

		local_active = edata->active;
		FRAMEWORK_EVTHREAD_UNLOCK(edata);
	}
	TRACE("Shut down evdev thread.\n");

	FRAMEWORK_EVSESSION_LOCK(edata);
	TRACE("evdev thread adding SHUTDOWN flag\n");
	edata->flags |= FRAMEWORK_EVSESSION_FLAG_SHUTDOWN;
	FRAMEWORK_EVSESSION_UNLOCK(edata);
	
	FRAMEWORK_EVTHREAD_LOCK(edata);
	TRACE("evdev thread wakeup on evdev_client channel\n");

	FRAMEWORK_EVSESSION_LOCK(edata);
	TRACE("evdev thread removing THREAD flag\n");
	edata->flags &= ~(FRAMEWORK_EVSESSION_FLAG_THREAD);
	FRAMEWORK_EVSESSION_UNLOCK(edata);
	
	wakeup(edata->evdev_client);
	FRAMEWORK_EVTHREAD_UNLOCK(edata);

	/* Terminate thread */
	kthread_exit();
}

static void
framework_evthread_dtor(void *data)
{
	struct evdev_client *client = (struct evdev_client *) data;

	TRACE("evdev thread dtor lock begin\n");
	EVDEV_LIST_LOCK(client->ec_evdev);
	if (!client->ec_revoked) {
		evdev_dispose_client(client->ec_evdev, client);

		TRACE("evdev thread revoking\n");	
		/* make sure we don't repeat disposal */
		evdev_revoke_client(client);
	}
	EVDEV_LIST_UNLOCK(client->ec_evdev);
	TRACE("evdev thread dtor lock end\n");

	/* clear kqueue - should be locked */
	EVDEV_CLIENT_LOCKQ(client);
	framework_evthread_clearkqueue(client);
	EVDEV_CLIENT_UNLOCKQ(client);

	TRACE("evdev thread dtor completed\n");
}

/*
 * Set callback function
 */
void
framework_evthread_setcb(struct framework_evdev_thread_t *ethread,
			 framework_evdev_thread_cbfunc cbfunc)
{
	FRAMEWORK_EVSESSION_LOCK(ethread);
	ethread->cbfunc = cbfunc;
	FRAMEWORK_EVSESSION_UNLOCK(ethread);
}

/*
 * Called to initialize the event thread
 */
struct framework_evdev_thread_t *
framework_evthread_init(size_t buffer_size, struct evdev_dev *evdev, void *ctx)
{
	struct framework_evdev_thread_t *ethread = NULL;
	

	ethread = malloc(sizeof(struct framework_evdev_thread_t), M_FRAMEWORK, M_WAITOK | M_ZERO);

	ethread->ctx = ctx;
	ethread->cbfunc = NULL;
	ethread->flags = 0;
	
	ethread->active = true;
	mtx_init(&ethread->session, "framework_evdev_session", NULL, MTX_DEF);

	ethread->evdev_client = malloc(offsetof(struct evdev_client, ec_buffer) +
				       sizeof(struct input_event) * buffer_size,
				       M_FRAMEWORK,
				       M_WAITOK | M_ZERO);

	ethread->evdev_client->ec_evdev = evdev;
	
	mtx_init(&ethread->evdev_client->ec_buffer_mtx,
		 "framework_evdev_client", NULL, MTX_DEF);

	ethread->evdev_client->ec_buffer_size = buffer_size;
	
	/* Set client to blocked, so to ensure we receive wakeups */
	ethread->evdev_client->ec_blocked = true;
	/* point selinfo mutex to client mutex */
	ethread->evdev_client->ec_selp.si_mtx = &ethread->evdev_client->ec_buffer_mtx;
	/* init knote list */
	knlist_init_mtx(&ethread->evdev_client->ec_selp.si_note,
			&ethread->evdev_client->ec_buffer_mtx);
	ethread->flags |= FRAMEWORK_EVSESSION_FLAG_KQUEUE;
	devfs_set_cdevpriv(ethread->evdev_client, framework_evthread_dtor);
	
	int error = kthread_add(framework_evthread_func, ethread, NULL,
				NULL, 0, 0,
				"framework_evdev_thread");

	if (0 != error) {
		ERROR("kthread_add returned error code %d\n", error);
	}

	TRACE("evdev thread init completed\n");

	return ethread;
}

/*
 * Register as client at evdev
 */
int
framework_evthread_registerclient(struct framework_evdev_thread_t *ethread,
				  struct evdev_dev *dev)
{
	int error = 0;
	
	if (!(framework_evthread_getflags(ethread) & FRAMEWORK_EVSESSION_FLAG_CLIENTREG)) {
		TRACE("evdev thread registering client\n");
		EVDEV_LIST_LOCK_SIG(dev);
		error = evdev_register_client(dev, ethread->evdev_client);
		EVDEV_LIST_UNLOCK(dev);
		TRACE("evdev thread client registration completed\n");
		
		FRAMEWORK_EVSESSION_LOCK(ethread);
		TRACE("evdev thread adding CLIENTREG flag\n");
		ethread->flags |= FRAMEWORK_EVSESSION_FLAG_CLIENTREG;
		FRAMEWORK_EVSESSION_UNLOCK(ethread);
	} else {
		ERROR("unable to register client because CLIENTREG flag unset\n");
	}

	return error;
}

/*
 * Unregister as client at evdev
 */
int
framework_evthread_unregisterclient(struct framework_evdev_thread_t *ethread,
				    struct evdev_dev *dev)
{
	if (framework_evthread_getflags(ethread) & FRAMEWORK_EVSESSION_FLAG_CLIENTREG) {
		framework_evthread_dtor(ethread->evdev_client);
		
		FRAMEWORK_EVSESSION_LOCK(ethread);
		TRACE("evdev thread removing CLIENTREG flag\n");
		ethread->flags &= ~(FRAMEWORK_EVSESSION_FLAG_CLIENTREG);
		FRAMEWORK_EVSESSION_UNLOCK(ethread);
	}
	return 0;
}

/*
 * Called to destroy the event thread
 */
int
framework_evthread_destroy(struct framework_evdev_thread_t *ethread)
{
	TRACE("Switching thread %p to inactive\n", ethread);

	if (NULL == ethread)
		return (EINVAL);

	FRAMEWORK_EVSESSION_LOCK(ethread);
	ethread->active = false;
	ethread->cbfunc = NULL;
	FRAMEWORK_EVSESSION_UNLOCK(ethread);
     
	/* Wake up and terminate listener thread */
	FRAMEWORK_EVTHREAD_LOCK(ethread);
	TRACE("Waking up thread at %p\n", ethread->evdev_client);
	if (framework_evthread_getflags(ethread) & FRAMEWORK_EVSESSION_FLAG_THREAD) {
		wakeup(ethread->evdev_client);

		if (!(ethread->flags & FRAMEWORK_EVSESSION_FLAG_SHUTDOWN)) {
			TRACE("evdev thread waiting for thread completion\n");
			/* last we know, thread is still running, sleep and wait */
			msleep(ethread->evdev_client,
			       &ethread->evdev_client->ec_buffer_mtx,
			       0, "sigwait", 0);
			TRACE("evdev thread awoke destroy func\n");
		}
	}
	FRAMEWORK_EVTHREAD_UNLOCK(ethread);

	/* if flag for client registration is set and client needs removal */
	if (ethread->evdev_client->ec_evdev) {
		if (framework_evthread_getflags(ethread) & FRAMEWORK_EVSESSION_FLAG_CLIENTREG) {
			TRACE("evdev thread locking evdev list\n");
			EVDEV_LIST_LOCK(ethread->evdev_client->ec_evdev);
			if (!ethread->evdev_client->ec_revoked) {
				TRACE("evdev thread disposing evdev client\n");
				evdev_dispose_client(ethread->evdev_client->ec_evdev,
						     ethread->evdev_client);
				evdev_revoke_client(ethread->evdev_client);
			}
			EVDEV_LIST_UNLOCK(ethread->evdev_client->ec_evdev);

			TRACE("evdev thread removing CLIENTREG flag\n");
			FRAMEWORK_EVSESSION_LOCK(ethread);
			ethread->flags &= ~(FRAMEWORK_EVSESSION_FLAG_CLIENTREG);
			FRAMEWORK_EVSESSION_UNLOCK(ethread);
			TRACE("evdev thread completed CLIENTREG flag removal\n");
		}
	}

	TRACE("evdev thread clearing kqueue\n");
	FRAMEWORK_EVTHREAD_LOCK(ethread);
	framework_evthread_clearkqueue(ethread->evdev_client);
	FRAMEWORK_EVTHREAD_UNLOCK(ethread);
	TRACE("evdev thread removing KQUEUE flag\n");
	FRAMEWORK_EVSESSION_LOCK(ethread);
	ethread->flags &= ~(FRAMEWORK_EVSESSION_FLAG_KQUEUE);
	FRAMEWORK_EVSESSION_UNLOCK(ethread);
	TRACE("evdev thread destroying knlist\n");
	knlist_destroy(&ethread->evdev_client->ec_selp.si_note);

	TRACE("evdev thread destroying mutexes\n");
	mtx_destroy(&ethread->session);
	mtx_destroy(&ethread->evdev_client->ec_buffer_mtx);

	TRACE("evdev thread freeing client\n");
	free(ethread->evdev_client, M_FRAMEWORK);
	TRACE("evdev thread freeing main thread\n");
	free(ethread, M_FRAMEWORK);
	
	return 0;
}

