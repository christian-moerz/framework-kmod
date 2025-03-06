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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/queue.h>

#include <dbus/dbus.h>

#define DBUS_IFACE_NAME "org.freedesktop.DBus.Properties"
#define DBUS_NAMELEN          255
#define DBUS_PARAM2LEN        255
#define DBUS_PLAYBACKSTATELEN 255

#define DBUS_LEVEL_ENTRY 0

#define DEBUG(...)

/* Original signal handler */
static sig_t orig_sig = 0;

/* Active flag */
bool active = true;

/* Forward declaration */
struct dbus_message_signal_t;

/* type definition for handler function */
typedef void(*dbus_handler_func_t)(struct dbus_message_signal_t *);

/*
 * Parsed DBUS message signal
 */
struct dbus_message_signal_t {
	const char *interface;
	const char *path;
	const char *sender;
	const char *signature;
	char name[DBUS_NAMELEN];
	char param2[DBUS_PARAM2LEN];
	char playback_status[DBUS_PLAYBACKSTATELEN];
};

/*
 * Represents state of a known playback program
 */
struct dbus_player_t {
	char name[DBUS_NAMELEN];
	uint8_t state;

	LIST_ENTRY(dbus_player_t) entries;
};

LIST_HEAD(dbus_player_list_t, dbus_player_t) dbus_players =
	LIST_HEAD_INITIALIZER(dbus_players);

struct dbus_message_handlermap_t {
	const char *state_name;
	dbus_handler_func_t func;
};

/* Forward declaration for start function */
void signal_start(struct dbus_message_signal_t *);
/* Forward declaration for stop function */
void signal_stop(struct dbus_message_signal_t *);

struct dbus_message_handlermap_t handler_map[] = {
	{
		.state_name = "PLAYING",
		.func = signal_start
	},
	{
		.state_name = "PAUSED",
		.func = signal_stop
	},
	{
		.state_name = "STOPPED",
		.func = signal_stop
	}
};

/*
 * Make string uppercase
 */
void
makeupper(char *pstr)
{
	while (*pstr) {
		*pstr = toupper(*pstr);
		
		pstr++;
	}
}

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
 * Look up player on list; add if it does not exist
 */
struct dbus_player_t *
find_player(const char *name)
{
	struct dbus_player_t *player = 0;
	
	LIST_FOREACH(player, &dbus_players, entries) {
		if (!strncmp(player->name, name, DBUS_NAMELEN - 1)) {
			return player;
		}
	}

	player = malloc(sizeof(struct dbus_player_t));
	if (!player)
		return NULL;
	bzero(player, sizeof(struct dbus_player_t));
	strncpy(player->name, name, DBUS_NAMELEN - 1);
	LIST_INSERT_HEAD(&dbus_players, player, entries);

	return player;
}

/*
 * Remove a player from the list
 */
void
rm_player(struct dbus_player_t *player)
{
	LIST_REMOVE(player, entries);
	free(player);
}

/*
 * Remove all players on list
 */
void
clear_players()
{
	struct dbus_player_t *player = 0;

	while (!LIST_EMPTY(&dbus_players)) {
		player = LIST_FIRST(&dbus_players);
		LIST_REMOVE(player, entries);
		free(player);
	}
}

/*
 * Handles playback start
 */
void
signal_start(struct dbus_message_signal_t *parsed_msg)
{
	struct dbus_player_t *player = find_player(parsed_msg->name);

	if (!player) {
		err(EX_OSERR, "Failed to allocate player data");
	}

	if (0 == player->state) {
		/* Only if we aren't already playing, we start playback */
		player->state = 1;
		printf("framework-dbus: Playback started.\n");
	}
}

/*
 * Handles playback stop or pausing
 */
void
signal_stop(struct dbus_message_signal_t *parsed_msg)
{
	struct dbus_player_t *player = find_player(parsed_msg->name);

	if (!player) {
		err(EX_OSERR, "Failed to allocate player data");
	}

	if (1 == player->state) {
		/* Only if we are still playing, we stop playback */
		player->state = 0;
		printf("framework-dbus: Playback stopped\n");
		rm_player(player);
	}	
}

void
print_signal(struct dbus_message_signal_t *parsed_msg)
{
	printf("parsed dbus message\n");
	printf("\tinterface: %s\n", parsed_msg->interface);
	printf("\tpath: %s\n", parsed_msg->path);
	printf("\tsender: %s\n", parsed_msg->sender);
	printf("\tsignature: %s\n", parsed_msg->signature);
	printf("\tname: %s\n", parsed_msg->name);
	printf("\tplayback status: %s\n", parsed_msg->playback_status);
}

/*
 * Parse sub types
 */
int
parse_signal_iter(DBusMessageIter *iter, uint8_t sub_level,
		  struct dbus_message_signal_t *parsed_msg)
{
	uint8_t counter = 0, subcounter = 0;
	DBusBasicValue basic_value = {0};
	DBusMessageIter subiter = {0};
	int result = 0;

	do {
		switch (dbus_message_iter_get_arg_type(iter)) {
		case DBUS_TYPE_DICT_ENTRY:
			DEBUG("\t[%d] arg %d: dict entry\n", sub_level, counter);
			dbus_message_iter_recurse(iter, &subiter);
			result |= parse_signal_iter(&subiter, sub_level + 1, parsed_msg);
			break;
		case DBUS_TYPE_ARRAY:
			DEBUG("\t[%d] arg %d: array\n", sub_level, counter);
			dbus_message_iter_recurse(iter, &subiter);
			result |= parse_signal_iter(&subiter, sub_level + 1, parsed_msg);
			break;
		case DBUS_TYPE_VARIANT:
			DEBUG("\t[%d] arg %d: variant\n", sub_level, counter);
			dbus_message_iter_recurse(iter, &subiter);
			result |= parse_signal_iter(&subiter, sub_level + 1, parsed_msg);
			break;
		case DBUS_TYPE_STRING:
			dbus_message_iter_get_basic(iter, &basic_value);
			DEBUG("\t[%d] arg %d: string \"%s\"\n",
			       sub_level, counter, basic_value.str);

			if ((DBUS_LEVEL_ENTRY == sub_level) && (0 == counter)) {
				bzero(parsed_msg->name, DBUS_NAMELEN);
				strncpy(parsed_msg->name, 
					basic_value.str, DBUS_NAMELEN - 1);
			}
			if ((2 == sub_level) && (0 == counter)) {
				bzero(parsed_msg->param2, DBUS_PARAM2LEN);
				strncpy(parsed_msg->param2,
					basic_value.str, DBUS_PARAM2LEN - 1);
			}
			if ((3 == sub_level) && (0 == counter)) {
				bzero(parsed_msg->playback_status, DBUS_PLAYBACKSTATELEN);
				strncpy(parsed_msg->playback_status,
					basic_value.str, DBUS_PLAYBACKSTATELEN - 1);
				makeupper(parsed_msg->playback_status);
			}
			break;
		default:
			DEBUG("\t[%d], arg %d: unknown type %d\n",
			      sub_level, counter,
			      dbus_message_iter_get_arg_type(iter));
			break;
		}
		
		counter++;
	} while (dbus_message_iter_next(iter));
	
	return result;
}

/*
 * Attempt to parse a message
 */
int
parse_signal(DBusMessage *msg, struct dbus_message_signal_t *parsed_msg)
{
	DBusMessageIter args = {0};
	DBusMessageIter array_args = {0};
	uint8_t counter = 0, subcounter = 0;
	DBusBasicValue basic_value = {0};

	if (!dbus_message_iter_init(msg, &args)) {
		printf("framework-dbus: failed to parse signal\n");
		return -1;
	}

	parsed_msg->interface = dbus_message_get_interface(msg);
	parsed_msg->signature = dbus_message_get_signature(msg);
	parsed_msg->path = dbus_message_get_path(msg);
	parsed_msg->sender = dbus_message_get_sender(msg);

	return parse_signal_iter(&args, DBUS_LEVEL_ENTRY, parsed_msg);
}

void
choose_handler(struct dbus_message_signal_t *parsed_msg)
{
	size_t counter = 0, array_len = sizeof(handler_map) /
		sizeof(struct dbus_message_handlermap_t);

	if (!parsed_msg)
		return;
	if (!*parsed_msg->playback_status)
		return;

	for (counter = 0; counter < array_len; counter++) {
		if (!strncmp(parsed_msg->playback_status,
			     handler_map[counter].state_name,
			     DBUS_PLAYBACKSTATELEN - 1)) {
			if (handler_map[counter].func)
				handler_map[counter].func(parsed_msg);
		}
	}
}

/*
 * Program entry point
 */
int
main(int argc, char **argv)
{
	DBusError error = {0};
	DBusConnection *conn = 0;
	DBusMessage *msg = 0;
	int result = 0;
	struct dbus_message_signal_t parsed_msg = {0};

	/* redirect signal handling */
	orig_sig = signal(SIGINT, sigfunc);
	signal(SIGTERM, sigfunc);

	dbus_error_init(&error);

	conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
	if (dbus_error_is_set(&error)) {
		err(EX_UNAVAILABLE,
		    "framework-dbus: failed to connect to dbus session\n");
		
	}

	/* subscribe to relevant messages */
	dbus_bus_add_match(conn, 
			   "type='signal',"
			   "interface='" DBUS_IFACE_NAME "',"
			   "path='/org/mpris/MediaPlayer2'", 
			   &error); // see signals from the given interface
	dbus_connection_flush(conn);
	if (dbus_error_is_set(&error)) { 
		err(EX_UNAVAILABLE,
		    "Match Error (%s)\n", error.message);
	}

	while (active) {
		dbus_connection_read_write(conn, 0);
		
		/* Reset structure */
		bzero(&parsed_msg, sizeof(parsed_msg));
		
		msg = dbus_connection_pop_message(conn);

		// loop again if we haven't read a message
		if (NULL == msg) { 
			sleep(1);
			continue;
		}

		if (dbus_message_is_signal(msg,
					   "org.freedesktop.DBus.Properties",
					   "PropertiesChanged")) {

			result = parse_signal(msg, &parsed_msg);
		}

		/* print_signal(&parsed_msg); */
		choose_handler(&parsed_msg);
		
		/* free the message */
		dbus_message_unref(msg);
	}

	/* shared access, therefore not calling dbus_connection_close */

	/* Clear any remaining players on list */
	clear_players();

	return 0;
}
