/*
 * routines to handle single client/port data
 *
 *  Copyright (c) 1999 by Takashi Iwai <iwai@ww.uni-erlangen.de>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "portlib.h"


/*
 * callbacks
 */
#define PORT_NUM_CBS	(PORT_MIDI_EVENT_CB + 1)

typedef struct port_callback_list_t port_callback_list_t;

typedef struct {
	port_callback_t func;
	void *private_data;
} cb_pair_t;

/*
 * client data
 */
struct port_client_t {
	snd_seq_t *seq;
	int client;
	int mode;
	int num_ports;
	port_t *ports;
};

struct port_t {
	port_client_t *client;
	int port;
	cb_pair_t callback[PORT_NUM_CBS];
	int num_subscribed;
	int num_used;
	struct port_t *next;
};

/*
 * prototypes
 */
static void error(char *msg);
static int call_callbacks(port_client_t *client, snd_seq_event_t *ev);


/*
 * output error message and exit
 */
static void error(char *msg)
{
	perror(msg);
	exit(1);
}

#if SND_LIB_MINOR > 5
static void error_handler(const char *file, int line, const char *func, int err, const char *fmt, ...)
{
	/* ignore.. */
}
#ifdef snd_seq_client_info_alloca
#define ALSA_API_ENCAP
#endif
#endif


/*
 * create a client by non-blocking mode
 */
port_client_t *port_client_new(char *name, int mode)
{
	port_client_t *client;

	if ((client = malloc(sizeof(*client))) == NULL)
		error("can't malloc");
	memset(client, 0, sizeof(*client));
#if SND_LIB_MINOR > 5
	snd_lib_error_set_handler(error_handler);
	if (snd_seq_open(&client->seq, "hw", mode, SND_SEQ_NONBLOCK) < 0)
		error("open seq");
#else
	if (snd_seq_open(&client->seq, mode) < 0)
		error("open seq");
	snd_seq_block_mode(client->seq, 0);
#endif
	client->client = snd_seq_client_id(client->seq);
	client->mode = mode;
	client->num_ports = 0;
	client->ports = NULL;
	
	if (snd_seq_set_client_name(client->seq, name) < 0)
		error("set client info");

	return client;
}

/*
 * delete the client
 */
void port_client_delete(port_client_t *client)
{
	if (client) {
		port_t *p, *next;
		snd_seq_close(client->seq);
		for (p = client->ports; p; p = next) {
			next = p->next;
			free(p);
		}
		free(client);
	}
}

/*
 * attach a port
 * capability is set to both to group and all clients.
 */
port_t *port_attach(port_client_t *client, char *name, unsigned int cap, unsigned int type)
{
	port_t *p, *q;

	p = malloc(sizeof(*p));
	if (p == NULL)
		error("can't malloc");
	memset(p, 0, sizeof(*p));
	p->client = client;

	p->port = snd_seq_create_simple_port(client->seq, name, cap, type);
	if (p->port < 0)
		error("create port");

	client->num_ports++;
	if (client->ports == NULL)
		client->ports = p;
	else {
		for (q = client->ports; q->next; q = q->next)
			;
		q->next = p;
	}

	return p;
}

/*
 * detach the port
 */
int port_detach(port_t *p)
{
	port_client_t *client;
	port_t *q, *prev;

	if (snd_seq_delete_simple_port(p->client->seq, p->port) < 0)
		error("delete port");

	client = p->client;
	prev = NULL;
	for (q = client->ports; q; prev = q, q = q->next) {
		if (q == p) {
			if (prev)
				prev->next = q->next;
			else
				client->ports = q->next;
			client->num_ports--;
			break;
		}
	}
	free(p);
	return 0;
}

/*
 * main loop
 */
void port_client_do_loop(port_client_t *client)
{
#if SND_LIB_MINOR > 5
	int npfds = snd_seq_poll_descriptors_count(client->seq, POLLIN);
	struct pollfd *pfd;
	if (npfds <= 0)
		return;
	pfd = alloca(sizeof(*pfd) * npfds);
	if (snd_seq_poll_descriptors(client->seq, pfd, npfds, POLLIN) < 0)
		return;
	for (;;) {
		if (poll(pfd, npfds, -1) < 0)
			error("poll");
		if (port_client_do_event(client))
			break;
	}
#else
	fd_set rfds;
	int fd = snd_seq_file_descriptor(client->seq);
	
	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		if (select(fd + 1, &rfds, NULL, NULL, NULL) < 0)
			error("select");
		if (FD_ISSET(fd, &rfds)) {
			if (port_client_do_event(client))
				break;
		}
	}
#endif
}


/*
 * do one event
 */
int
port_client_do_event(port_client_t *client)
{
	snd_seq_event_t *ev;
	int rc, cells;

	while ((cells = snd_seq_event_input(client->seq, &ev)) >= 0 && ev != NULL) {
		rc = call_callbacks(client, ev);
		snd_seq_free_event(ev);
		if (rc < 0)
			return rc;
	}
	snd_seq_flush_output(client->seq);
	return 0;
}


/*
 * search specified port
 */
port_t *port_client_search_port(port_client_t *client, int port)
{
	port_t *p;

	for (p = client->ports; p; p = p->next)
		if (p->port == port)
			return p;
	return NULL;
}


/*
 * call a callback function
 */
static int call_callbacks(port_client_t *client, snd_seq_event_t *ev)
{
	port_t *p = port_client_search_port(client, ev->dest.port);

	if (p == NULL)
		return 0;

	switch (ev->type) {
#ifdef ALSA_API_ENCAP
#define snd_seq_addr_equal(a,b)	((a)->client == (b)->client && (a)->port == (b)->port)
	case SND_SEQ_EVENT_PORT_SUBSCRIBED:
		if (snd_seq_addr_equal(&ev->data.connect.sender, &ev->dest)) {
			p->num_subscribed++;
			return port_call_callback(p, PORT_SUBSCRIBE_CB, ev);
		} else if (snd_seq_addr_equal(&ev->data.connect.dest, &ev->dest)) {
			p->num_used++;
			return port_call_callback(p, PORT_USE_CB, ev);
		}
		break;
	case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
		if (snd_seq_addr_equal(&ev->data.connect.sender, &ev->dest)) {
			p->num_subscribed--;
			return port_call_callback(p, PORT_UNSUBSCRIBE_CB, ev);
		} else if (snd_seq_addr_equal(&ev->data.connect.dest, &ev->dest)) {
			p->num_used--;
			return port_call_callback(p, PORT_UNUSE_CB, ev);
		}
		break;
#else
	case SND_SEQ_EVENT_PORT_SUBSCRIBED:
		p->num_subscribed++;
		return port_call_callback(p, PORT_SUBSCRIBE_CB, ev);
	case SND_SEQ_EVENT_PORT_USED:
		p->num_used++;
		return port_call_callback(p, PORT_USE_CB, ev);
	case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
		p->num_subscribed--;
		return port_call_callback(p, PORT_UNSUBSCRIBE_CB, ev);
	case SND_SEQ_EVENT_PORT_UNUSED:
		p->num_used--;
		return port_call_callback(p, PORT_UNUSE_CB, ev);
#endif
	default:
		return port_call_callback(p, PORT_MIDI_EVENT_CB, ev);
	}
	return 0;
}

/*
 * exported methods
 */
int port_client_get_id(port_client_t *client)
{
	return client->client;
}

snd_seq_t *port_client_get_seq(port_client_t *client)
{
	return client->seq;
}

port_client_t *port_get_client(port_t *p)
{
	return p->client;
}

int port_get_port(port_t *p)
{
	return p->port;
}

/*
 * call a specified callback
 */
int port_call_callback(port_t *p, int cb, snd_seq_event_t *ev)
{
	if (cb < 0 || cb >= PORT_NUM_CBS)
		error("invalid callback");
	if (ev == NULL)
		return 0;
	if (p->callback[cb].func)
		return p->callback[cb].func(p, cb, ev, p->callback[cb].private_data);
	return 0;
}

/*
 * write an event
 */
int port_write_event(port_t *p, snd_seq_event_t *ev, int flush)
{
	int rc;

	snd_seq_ev_set_source(ev, p->port);
	if ((rc = snd_seq_event_output(p->client->seq, ev)) < 0)
		return rc;
	if (flush)
		snd_seq_flush_output(p->client->seq);
	return rc;
}

/*
 * flush events
 */
int port_flush_event(port_t *p)
{
	return snd_seq_flush_output(p->client->seq);
}

/*
 * add a callback function to the port
 */
int port_add_callback(port_t *p, int cb, port_callback_t func, void *private_data)
{
	if (cb < 0 || cb >= PORT_NUM_CBS)
		error("invalid callback");
	p->callback[cb].func = func;
	p->callback[cb].private_data = private_data;
	return 0;
}

/*
 * remove callback function from the port
 */
int port_remove_callback(port_t *p, int cb)
{
	if (cb < 0 || cb >= PORT_NUM_CBS)
		error("invalid callback");
	p->callback[cb].func = NULL;
	p->callback[cb].private_data = NULL;
	return 0;
}

/*
 * subscribe from this port to the specified port (write)
 */
int port_connect_to(port_t *p, int client, int port)
{
	int rc;

	rc = snd_seq_connect_to(p->client->seq, p->port, client, port);
	if (rc >= 0) {
		p->num_subscribed++;
		if (client == p->client->client)
			p->num_used++;
	}
	return rc;
}

/*
 * subscribe from the specified port to this port (read)
 */
int port_connect_from(port_t *p, int client, int port)
{
	int rc;

	rc = snd_seq_connect_from(p->client->seq, p->port, client, port);
	if (rc >= 0) {
		p->num_used++;
		if (client == p->client->client)
			p->num_subscribed++;
	}
	return rc;
}

/*
 */
int port_num_subscription(port_t *p, int type)
{
	switch (type) {
	case SND_SEQ_QUERY_SUBS_READ:
		return p->num_subscribed;
	case SND_SEQ_QUERY_SUBS_WRITE:
		return p->num_used;
	}
	return 0;
}
