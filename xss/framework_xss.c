/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021-2023 Christian Moerz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

/* Original signal handler */
static sig_t orig_sig = 0;

/* Active flag */
bool active = true;

/*
 * Signal handler function
 */
void
sigfunc(int signum)
{
	switch (signum) {
	case SIGTERM:
	case SIGINT:
		active = false;
		break;
	default:
		break;
	}
}

/*
 * Retrieve and print screen saver infos
 */
void
print_saverinfo(Display *dsp, int *prev_state)
{
	XScreenSaverInfo *si = 0;

	/* redirect signal handling */
	orig_sig = signal(SIGINT, sigfunc);
	signal(SIGTERM, sigfunc);
	
	si = XScreenSaverAllocInfo();

	if (NULL == si) {
		err(EX_UNAVAILABLE, "Failed to allocate memory\n");
	}

	if (0 != XScreenSaverQueryInfo(dsp, DefaultRootWindow(dsp), si)) {
		if (si->state == *prev_state) {
			goto done;
		}
		*prev_state = si->state;
		
		switch (si->state) {
		case ScreenSaverOn:
			printf("\tscreen saver is on\n");
			break;
		case ScreenSaverOff:
			printf("\tscreen saver is off\n");
			break;
		case ScreenSaverDisabled:
			printf("\tscreen saver is disabled\n");
			break;
		default:
			printf("\tunknown state %d\n", si->state);
			break;
		}

		switch(si->kind) {
		case ScreenSaverBlanked:
			printf("\tusing blanked mode\n");
			break;
		case ScreenSaverInternal:
			printf("\tusing internal mode\n");
			break;
		case ScreenSaverExternal:
			printf("\tusing external mode\n");
			break;
		default:
			printf("\tusing known mode %d\n", si->kind);
			break;
		}

		printf("\ttil_or_since: %lu\n", si->til_or_since);
		printf("\tidle: %lu\n", si->idle);
		printf("\teventMask: %lu\n", si->eventMask);
	} else {
		printf("framework-xss: Failed to retrieve screen saver info\n");
	}

done:

	XFree(si);
}

/*
 * Program entry point
 */
int
main(int argc, char **argv)
{
	Display *dsp = XOpenDisplay(NULL);
	int major = 0, minor = 0;
	int event_base = 0, error_base = 0;
	XEvent event = {0};
	int state = -1;

	if (NULL == dsp) {
		err(EX_OSERR, "framework-xss: failed to open display\n");
	}

	if (!XQueryExtension(dsp, ScreenSaverName, &major, &event_base,
			     &error_base)) {
		err(EX_UNAVAILABLE, "framework-xss: extension not available\n");
	}

	if (XScreenSaverQueryVersion(dsp, &major, &minor)) {
		printf("framework-xss: found version %d.%d\n",
		       major, minor);
	}

	if (!XScreenSaverQueryExtension(dsp, &event_base, &error_base)) {
		err(EX_OSERR, "framework-xss: Xss extension unavailable\n");
	} else {
		printf("framework-xss: event_base=%d, error_base=%d\n",
		       event_base, error_base);
	}

	/* select both event types */
	XScreenSaverSelectInput(dsp, DefaultRootWindow(dsp),
				ScreenSaverNotifyMask | ScreenSaverCycleMask);


	while (active) {
		print_saverinfo(dsp, &state);

		if (!XCheckTypedEvent(dsp,
				     ScreenSaverNotifyMask | ScreenSaverCycleMask,
				     &event)) {
			sleep(1);
		}
	}

	XCloseDisplay(dsp);
}
