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

#include <sys/malloc.h>

#include "framework_state.h"
#include "framework_utils.h"

struct framework_state_t {
	struct mtx lock;          /* l - locking mechanism */

	uint8_t flags;            /* structure state flags */

	uint32_t block_dim_count; /* (l) counter of hints that block dimming */
};

#define FRAMEWORK_STATE_INIT 1
#define FRAMEWORK_STATE_DESTROY 2

#define FRAMEWORK_STATE_LOCK(x) mtx_lock(&(x)->lock)
#define FRAMEWORK_STATE_UNLOCK(x) mtx_unlock(&(x)->lock)

MALLOC_DECLARE(M_FRAMEWORK);

/*
 * Initialize a state structure
 */
struct framework_state_t *
framework_state_init(void)
{
	struct framework_state_t *state =
		malloc(sizeof(struct framework_state_t), M_FRAMEWORK,
		       M_WAITOK | M_ZERO);
	
	mtx_init(&state->lock, "framework_state", 0, MTX_DEF);
	state->flags |= FRAMEWORK_STATE_INIT;
	
	return state;
}

/*
 * get current dim count
 */
uint32_t
framework_state_getdimcount(struct framework_state_t *state)
{
	if (NULL == state)
		return 0;

	uint32_t counter = 0;
	
	FRAMEWORK_STATE_LOCK(state);
	counter = state->block_dim_count;
	FRAMEWORK_STATE_UNLOCK(state);

	return counter;	
}

/*
 * Increment block counter
 */
void
framework_state_incdimcount(struct framework_state_t *state)
{
	if (NULL == state)
		return;

	FRAMEWORK_STATE_LOCK(state);
	state->block_dim_count++;
	FRAMEWORK_STATE_UNLOCK(state);
}

/*
 * Decrement block counter
 */
void
framework_state_decdimcount(struct framework_state_t *state)
{
	if (NULL == state)
		return;

	FRAMEWORK_STATE_LOCK(state);
	if (0 == state->block_dim_count) {
		ERROR("block_dim_count == 0 fails to decrement\n");
	} else {
		state->block_dim_count--;
	}
	FRAMEWORK_STATE_UNLOCK(state);	
}

/*
 * Destroy previously initialized state structure
 */
void
framework_state_destroy(struct framework_state_t *state)
{
	if (NULL == state)
		return;
	
	if (state->flags & FRAMEWORK_STATE_INIT) {
		if (!(state->flags & FRAMEWORK_STATE_DESTROY)) {
			state->flags |= FRAMEWORK_STATE_DESTROY;
			state->flags |= ~FRAMEWORK_STATE_INIT;
			mtx_destroy(&state->lock);

			free(state, M_FRAMEWORK);
		}		
	}
}
