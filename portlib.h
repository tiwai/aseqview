/*
 * routines to handle single client/port data
 *
 * Copyright (c) 1999 by Takashi Iwai <iwai@ww.uni-erlangen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef PORTLIB_H_DEF
#define PORTLIB_H_DEF

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
#include <alsa/asoundlib.h>
#else
#include <sys/asoundlib.h>
#endif

#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR >= 6
#define snd_seq_file_descriptor(x)	snd_seq_poll_descriptor(x)
#define snd_seq_flush_output(x)		snd_seq_drain_output(x)
#endif

typedef struct port_client_t port_client_t;
typedef struct port_t port_t;

/*
 * type of callbacks
 */
enum port_callback_type_t {
	PORT_SUBSCRIBE_CB,
	PORT_USE_CB,
	PORT_UNSUBSCRIBE_CB,
	PORT_UNUSE_CB,
	PORT_MIDI_EVENT_CB
};

typedef int (*port_callback_t)(port_t *p, int type, snd_seq_event_t *ev, void *private_data);

/*
 * capabilities
 */
#define PORT_CAP_RD	(SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ)
#define PORT_CAP_WR	(SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE)
#define PORT_CAP_ALL	(PORT_CAP_RD|PORT_CAP_WR)

port_client_t *port_client_new(char *name, int mode);
void port_client_delete(port_client_t *p);
port_t *port_attach(port_client_t *p, char *name, unsigned int cap, unsigned int type);
int port_detach(port_t *p);
void port_client_do_loop(port_client_t *p, int timeout);
int port_client_do_event(port_client_t *p);
port_t *port_client_search_port(port_client_t *client, int port);

port_client_t *port_get_client(port_t *c);
snd_seq_t *port_client_get_seq(port_client_t *c);
int port_client_get_id(port_client_t *p);
int port_get_port(port_t *p);
int port_client_get_port(port_client_t *c);
void port_client_stop(port_client_t *c);

int port_connect_to(port_t *p, int client, int port);
int port_connect_from(port_t *p, int client, int port);

int port_add_callback(port_t *p, int type, port_callback_t ptr, void *private_data);
int port_remove_callback(port_t *p, int type);
int port_call_callback(port_t *p, int type, snd_seq_event_t *ev);
int port_write_event(port_t *p, snd_seq_event_t *ev, int flush);
int port_flush_event(port_t *p);
int port_num_subscription(port_t *p, int type);

#endif
