/*
 * aseqview - ALSA sequencer event viewer / filter
 *
 * Copyright (c) 1999-2000 by Takashi Iwai <tiwai@suse.de>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include "levelbar.h"
#include "portlib.h"
#include "piano.h" // From swami.

#define KEY_UNKNOWN	(-1)
#define MIDI_CHANNELS	16
#define NUM_KEYS	128
#define NUM_CTRLS	128
#define MAX_MIDI_VALS	128
#define PROG_NAME_LEN	8

#if SND_LIB_MAJOR == 0 && SND_LIB_MINOR <= 5
#define MIDI_CTL_MSB_MAIN_VOLUME SND_MCTL_MSB_MAIN_VOLUME
#define MIDI_CTL_MSB_EXPRESSION SND_MCTL_MSB_EXPRESSION
#define MIDI_CTL_MSB_PAN SND_MCTL_MSB_PAN
#define MIDI_CTL_ALL_SOUNDS_OFF SND_MCTL_ALL_SOUNDS_OFF
#define MIDI_CTL_RESET_CONTROLLERS SND_MCTL_RESET_CONTROLLERS
#define MIDI_CTL_ALL_NOTES_OFF SND_MCTL_ALL_NOTES_OFF
#define MIDI_CTL_MSB_BANK SND_MCTL_MSB_BANK
#endif

#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
#ifdef snd_seq_client_info_alloca
#define ALSA_API_ENCAP
#endif
#endif

typedef struct midi_status_t midi_status_t;
typedef struct port_status_t port_status_t;
typedef struct channel_status_t channel_status_t;

struct channel_status_t {
	port_status_t *port;
	int ch;
	int mute;
	int is_drum;
	int prog;
	unsigned char vel[NUM_KEYS];
	unsigned char ctrl[NUM_CTRLS];
	char progname[PROG_NAME_LEN + 1];
	int pitch;
	int max_vel, max_vel_key;
	int max_vel_lv, max_vel_count;
	/* widgets */
	GtkWidget *w_chnum, *w_prog;
	GtkWidget *w_vel, *w_main, *w_exp;
	GtkWidget *w_pan, *w_pitch;
	GtkWidget *w_piano;
};

enum {
	MIDI_MODE_GM,
	MIDI_MODE_GS,
	MIDI_MODE_XG
};

struct port_status_t {
	midi_status_t *main;
	int index;
	port_t *port;
	channel_status_t ch[MIDI_CHANNELS];
};

struct midi_status_t {
	port_client_t *client;
	int num_ports;
	port_status_t *ports;

	/* common parameter */
	int pitch_adj;
	int vel_scale;
	int midi_mode;

	int timer_update;
	int queue;
	GtkWidget *w_midi_mode, *w_time;
	GdkPixmap *w_gm_xpm, *w_gs_xpm, *w_xg_xpm;
	GdkPixmap *w_gm_xpm_off, *w_gs_xpm_off, *w_xg_xpm_off;
};


enum {
	V_CHNUM = 0,
	V_PROG,
	V_VEL,
	V_MAIN,
	V_EXP,
	V_PAN,
	V_PITCH,
	V_PIANO,
	V_COLS
};

static int show_piano = TRUE;
static int aseqview_cols = V_COLS;


/*
 * prototypes
 */
static int set_realtime_priority(int policy);
static int parse_addr(char *arg, int *clientp, int *portp);
static void usage(void);
static void create_port_window(port_status_t *port);
static midi_status_t *midi_status_new(int num_ports);
static GtkWidget *create_pitch_changer(midi_status_t *st);
static void adjust_pitch(GtkAdjustment *adj, midi_status_t *st);
static GtkWidget *create_velocity_changer(midi_status_t *st);
static void adjust_velocity(GtkAdjustment *adj, midi_status_t *st);
static int is_redirect(port_status_t *port);
static void restart_notes(midi_status_t *st);
static GtkWidget *create_viewer(port_status_t *port);
static void create_viewer_titles(GtkWidget *table);
static void set_vel_bar_color(GtkWidget *w, int is_drum, int in_buf);
static void av_channel_update(GtkWidget *w, int val, int in_buf);
static void av_note_update(GtkWidget *w, int key, int note_on, int in_buf);
static void create_channel_viewer(GtkWidget *table, port_status_t *st, int ch);
static void mute_channel(GtkToggleButton *w, channel_status_t *chst);
static void send_notes_off(channel_status_t *chst);
static void resume_notes_on(channel_status_t *chst);
static void handle_input(gpointer data, gint source, GdkInputCondition condition);
static int port_subscribed(port_t *p, int type, snd_seq_event_t *ev, port_status_t *port);
static int port_unused(port_t *p, int type, snd_seq_event_t *ev, port_status_t *port);
static void redirect_event(port_status_t *port, snd_seq_event_t *ev);
static int process_event(port_t *p, int type, snd_seq_event_t *ev, port_status_t *st);
static void change_note(port_status_t *st, int ch, int key, int vel, int in_buf);
static void change_controller(port_status_t *st, int ch, int param, int value, int in_buf);
static void all_sounds_off(channel_status_t *st, int in_buf);
static void all_notes_off(channel_status_t *st, int in_buf);
static void reset_controllers(channel_status_t *chst, int in_buf);
static void reset_all(midi_status_t *st);
static void change_pitch(port_status_t *st, int ch, int value, int in_buf);
static void change_program(port_status_t *st, int ch, int prog, int in_buf);
static void parse_sysex(port_status_t *st, int len, unsigned char *buf, int in_buf);
static GtkWidget *display_midi_init(GtkWidget *window, midi_status_t *st);
static void display_midi_mode(midi_status_t *st, int in_buf);
static int expose_midi_mode(GtkWidget *w);

static void *midi_loop(void *arg);
static gboolean idle_cb(gpointer data);
static int get_file_desc(midi_status_t *st);

static void av_ringbuf_init(void);
static int av_ringbuf_write(int type, GtkWidget *w, long data);
static int av_ringbuf_read(int *type, GtkWidget **w, long *data);


/*
 * local common variables
 */
static int do_output = TRUE;
static int rt_prio = FALSE;
static int use_thread = TRUE;
static pthread_t midi_thread;


/*
 * main routine
 */
static struct option long_option[] = {
	{"nooutput", 0, NULL, 'o'},
	{"realtime", 0, NULL, 'r'},
	{"source", 1, NULL, 's'},
	{"dest", 1, NULL, 'd'},
	{"ports", 1, NULL, 'p'},
	{"help", 0, NULL, 'h'},
	{"thread", 0, NULL, 't'},
	{"nothread", 0, NULL, 'm'},
	{"nopiano", 0, NULL, 'P'},
	{NULL, 0, NULL, 0},
};

int main(int argc, char **argv)
{
	midi_status_t *st;
	int num_ports = 1;
	int src_client = -1, src_port;
	int dest_client = -1, dest_port;
	int c, p;

	gtk_init(&argc, &argv);

	while ((c = getopt_long(argc, argv, "ors:d:p:tmP", long_option, NULL)) != -1) {
		switch (c) {
		case 'o':
			do_output = FALSE;
			break;
		case 'r':
			rt_prio = TRUE;
			break;
		case 's':
			if (parse_addr(optarg, &src_client, &src_port) < 0) {
				fprintf(stderr, "invalid argument %s for -s\n", optarg);
				exit(1);
			}
			break;
		case 'd':
			if (parse_addr(optarg, &dest_client, &dest_port) < 0) {
				fprintf(stderr, "invalid argument %s for -d\n", optarg);
				exit(1);
			}
			break;
		case 'p':
			num_ports = atoi(optarg);
			break;
		case 't':
			use_thread = TRUE;
			break;
		case 'm':
			use_thread = FALSE;
			break;
		case 'P':
			show_piano = FALSE;
			aseqview_cols = V_COLS - 1;
			fprintf(stderr, "don't show piano..\n");
			break;
		default:
			usage();
			exit(1);
			break;
		}
	}

	if (num_ports < 1 || num_ports > 20)
		g_error("invalid port numbers %d\n", num_ports);

	/* create instance */
	st = midi_status_new(num_ports);

	for (p = 0; p < num_ports; p++) {
		port_status_t *port = &st->ports[p];

		/* create window */
		create_port_window(port);

		/* add callbacks */
		port_add_callback(port->port, PORT_SUBSCRIBE_CB,
				  (port_callback_t)port_subscribed, port);
		port_add_callback(port->port, PORT_UNUSE_CB,
				  (port_callback_t)port_unused, port);
		port_add_callback(port->port, PORT_MIDI_EVENT_CB,
				  (port_callback_t)process_event, port);
	}

	if (use_thread) {
		av_ringbuf_init();
		pthread_create(&midi_thread, NULL, midi_loop, st);
		gtk_idle_add(idle_cb, st);
	} else {
		gdk_input_add(get_file_desc(st), GDK_INPUT_READ, handle_input, st);
		if (rt_prio)
			set_realtime_priority(SCHED_FIFO);
	}

	/* explicit subscription to ports */
	if (src_client >= 0 && src_client != SND_SEQ_ADDRESS_SUBSCRIBERS)
		port_connect_from(st->ports[0].port, src_client, src_port);
	if (do_output && dest_client >= 0 && dest_client != SND_SEQ_ADDRESS_SUBSCRIBERS) {
		port_connect_to(st->ports[0].port, dest_client, dest_port);
		reset_all(st);
	}

	gtk_main();

	if (use_thread) {
		port_client_stop(st->client);
		pthread_join(midi_thread, NULL);
	}

	return 0;
}


/*
 */
static int get_file_desc(midi_status_t *st)
{
	int fd = -1;
#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
	int npfds = snd_seq_poll_descriptors_count(port_client_get_seq(st->client), POLLIN);
	struct pollfd pfd;
	if (npfds == 1 &&
	    snd_seq_poll_descriptors(port_client_get_seq(st->client), &pfd, npfds, POLLIN) >= 0)
		fd = pfd.fd;
#else
	fd = snd_seq_file_descriptor(port_client_get_seq(st->client));
#endif
	if (fd < 0) {
		fprintf(stderr, "cannot get file descriptor for ALSA sequencer inputs\n");
		exit(1);
	}
	return fd;
}

/*
 */

struct av_ringbuf {
	int type;
	GtkWidget *w;
	long data;
};

#define RINGBUF_SIZE	512
static struct av_ringbuf *ringbuf;
static int ringbuf_rdptr, ringbuf_wrptr;

static void av_ringbuf_init(void)
{
	ringbuf = (struct av_ringbuf *)malloc(sizeof(struct av_ringbuf) * RINGBUF_SIZE);
	if (! ringbuf) {
		fprintf(stderr, "cannot allocate ringbuffer\n");
		exit(1);
	}
	memset(ringbuf, 0, sizeof(struct av_ringbuf) * RINGBUF_SIZE);
	ringbuf_rdptr = ringbuf_wrptr = 0;
}

static int av_ringbuf_write(int type, GtkWidget *w, long data)
{
	int wp, nwp;
	wp = ringbuf_wrptr;
	nwp = (wp + 1) & (RINGBUF_SIZE - 1);
	if (ringbuf_rdptr == nwp)
		return 0;
	ringbuf_wrptr = nwp;
	ringbuf[wp].type = type;
	ringbuf[wp].w = w;
	ringbuf[wp].data = data;
	return 1;
}

static int av_ringbuf_read(int *type, GtkWidget **w, long *data)
{
	int r;
	if (ringbuf_rdptr == ringbuf_wrptr)
		return 0;
	r = ringbuf_rdptr;
	*type = ringbuf[r].type;
	*w = ringbuf[r].w;
	*data = ringbuf[r].data;
	r = (r + 1) & (RINGBUF_SIZE - 1);
	ringbuf_rdptr = r;
	return 1;
}

/*
 */
enum {
	VEL_COLOR,
	UPDATE_STATUS,
	NOTE_ON,
	NOTE_OFF,
	UPDATE_PGM,
	UPDATE_MODE
};

static void *midi_loop(void *arg)
{
	midi_status_t *st = (midi_status_t *)arg;

	if (rt_prio)
		set_realtime_priority(SCHED_FIFO);

	port_client_do_loop(st->client, 50);
	pthread_exit(NULL);
	return 0;
}

static gboolean
idle_cb(gpointer data)
{
	// midi_status_t *st = (midi_status_t *)data;
	GtkWidget *w;
	int type;
	long val;

	while (av_ringbuf_read(&type, &w, &val)) {
		switch (type) {
		case VEL_COLOR:
			set_vel_bar_color(w, val, 0);
			break;
		case UPDATE_STATUS:
			av_channel_update(w, val, 0);
			break;
		case UPDATE_PGM:
			gtk_label_set_text(GTK_LABEL(w), (char *)val);
			break;
		case UPDATE_MODE:
			expose_midi_mode(w);
			break;
		case NOTE_ON:
			av_note_update(w, val, 1, 0);
			break;
		case NOTE_OFF:
			av_note_update(w, val, 0, 0);
			break;
		}
	}
	usleep(1000);
	return TRUE;
}

/*
 * set realtime prority as maximum
 */
static int
set_realtime_priority(int policy)
{
	struct sched_param parm;
#ifdef HAVE_CAPABILITY_H
	cap_t cp;
	cap_value_t caps[] = { CAP_SETCAP, CAP_SYS_NICE };
#endif

#ifdef HAVE_CAPABILITY_H
	cp = cap_get_proc();
	cap_set_flag(cp, flag, 2, &caps, CAP_SET);
	cap_set_proc(cp);
#endif

        memset(&parm, 0, sizeof(parm));
	parm.sched_priority = sched_get_priority_max(policy);

	if (sched_setscheduler(0, policy, &parm) != 0) {
		perror("sched_setscheduler");
		exit(1);
	}
	return 0;
}

/*
 * parse client:port address from command line
 */
static int
parse_addr(char *arg, int *clientp, int *portp)
{
	char *p;
	if (isdigit(*arg)) {
		if ((p = strpbrk(arg, ":.")) == NULL)
			return -1;
		*clientp = atoi(arg);
		*portp = atoi(p + 1);
	} else {
		if (*arg == 's' || *arg == 'S') {
			*clientp = SND_SEQ_ADDRESS_SUBSCRIBERS;
			*portp = 0;
		} else
			return -1;
	}
	return 0;
}

/*
 */
static int
quit(GtkWidget *w)
{
	gtk_exit(0);
	return FALSE;
}

/*
 * create main window
 */
static void
create_port_window(port_status_t *port)
{
	GtkWidget *toplevel;
	GtkWidget *vbox, *vbox2, *hbox, *w;
	char name[64];

	toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	sprintf(name, "ASeqView %d:%d",
		port_client_get_id(port->main->client), port->index);
	gtk_widget_set_name(toplevel, name);
	sprintf(name, "ALSA Sequencer Viewer %d:%d",
		port_client_get_id(port->main->client), port->index);
	gtk_window_set_title(GTK_WINDOW(toplevel), name);
	gtk_window_set_wmclass(GTK_WINDOW(toplevel), "aseqview", "ASeqView");

	gtk_signal_connect(GTK_OBJECT(toplevel), "delete_event",
			   GTK_SIGNAL_FUNC(quit), NULL);
	gtk_signal_connect(GTK_OBJECT(toplevel), "destroy",
			   GTK_SIGNAL_FUNC(quit), NULL);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(toplevel), vbox);
	gtk_widget_show(vbox);

	w = create_viewer(port);
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
	gtk_widget_show(toplevel);

	if (port->index != 0)
		return;

	/* control part */
	w = gtk_hseparator_new(); 
	gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);
	gtk_widget_show(w);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	w = display_midi_init(toplevel, port->main);
	gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
	
	w = gtk_vseparator_new(); 
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 0);
	gtk_widget_show(w);
		
	vbox2 = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);

	w = gtk_label_new("Pitch Correction");
	gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	w = create_pitch_changer(port->main);
	gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
	gtk_widget_show(vbox2);
	
	w = gtk_hseparator_new(); 
	gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, TRUE, 0);
	gtk_widget_show(w);

	w = gtk_label_new("Velocity Scale");
	gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	w = create_velocity_changer(port->main);
	gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
	gtk_widget_show(vbox2);
}

/*
 * print out usage
 */
static void
usage(void)
{
	printf("%s - ALSA sequencer event viewer / filter\n", PACKAGE);
	printf("  ver.%s\n", VERSION);
	printf("  Copyright (c) 1999-2005 by Takashi Iwai <tiwai@suse.de>\n");
	printf("\n");
		
	printf("usage: %s [-options]\n", PACKAGE);
	printf("   -o,--nooutput     suppress output (read-only mode)\n");
	printf("   -r,--realtime     set realtime priority (only for root)\n");
	printf("   -p,--ports #      number of ports to be opened\n");
	printf("   -s,--source addr  input from specified addr (client:port)\n");
	printf("   -d,--dest addr    output to specified addr (client:port)\n");
	printf("   -t,--thread       use multi-threads (default)\n");
	printf("   -m,--nothread     don't use multi-threads\n");
	printf("   -P,--nopiano      don't show piano\n");
}


/*
 * create midi_status_t instance;
 * sequencer is initialized here 
 */
static midi_status_t *
midi_status_new(int num_ports)
{
	int i, mode, p;
	unsigned int caps;
	midi_status_t *st;

	st = g_malloc0(sizeof(*st));
#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
	mode = do_output ? SND_SEQ_OPEN_DUPLEX : SND_SEQ_OPEN_INPUT;
#else
	mode = do_output ? SND_SEQ_OPEN : SND_SEQ_OPEN_IN;
#endif
	st->client = port_client_new("MIDI Viewer", mode);
	caps = SND_SEQ_PORT_CAP_WRITE |
		SND_SEQ_PORT_CAP_SUBS_WRITE;
	if (do_output)
		caps |= SND_SEQ_PORT_CAP_READ|
			SND_SEQ_PORT_CAP_SUBS_READ;
	
	st->num_ports = num_ports;
	st->ports = g_malloc0(sizeof(port_status_t) * num_ports);
	for (p = 0; p < num_ports; p++) {
		char name[32];
		port_status_t *port = &st->ports[p];

		port->main = st;
		port->index = p;
		sprintf(name, "Viewer Port %d", p);
		port->port = port_attach(st->client, name, caps,
					 SND_SEQ_PORT_TYPE_MIDI_GENERIC);

		/* initialize channels */
		for (i = 0; i < MIDI_CHANNELS; i++) {
			channel_status_t *chst = &port->ch[i];
			memset(chst, 0, sizeof(*chst));
			chst->port = port;
			chst->ch = i;
			chst->mute = 0;
			if (i == 9)
				chst->is_drum = 1;
			else
				chst->is_drum = 0;
			sprintf(chst->progname, "%3d", 0);
			chst->ctrl[MIDI_CTL_MSB_MAIN_VOLUME] = 100;
			chst->ctrl[MIDI_CTL_MSB_EXPRESSION] = 127;
			chst->ctrl[MIDI_CTL_MSB_PAN] = 64;
		}
	}

	return st;
}


/*
 * create pitch changer
 */
static GtkWidget *
create_pitch_changer(midi_status_t *st)
{
	GtkObject *adj;
	GtkWidget *pitch;

	adj = gtk_adjustment_new(0.0, -12.0, 12.0, 1.0, 1.0, 0.0);
	st->pitch_adj = 0;
	pitch = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_range_set_update_policy(GTK_RANGE(pitch),
				    GTK_UPDATE_DISCONTINUOUS);
	gtk_scale_set_digits(GTK_SCALE(pitch), 0);
	gtk_scale_set_value_pos(GTK_SCALE(pitch), GTK_POS_TOP);
	gtk_scale_set_draw_value(GTK_SCALE(pitch), TRUE);
	gtk_widget_show(pitch);

	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
			   GTK_SIGNAL_FUNC(adjust_pitch), st);

	return pitch;
}

/*
 * pitch up/down
 */
static void
adjust_pitch(GtkAdjustment *adj, midi_status_t *st)
{
	if (adj->value != st->pitch_adj) {
		st->pitch_adj = adj->value;
		restart_notes(st);
	}
}


/*
 * create velocity changer
 */
static GtkWidget *
create_velocity_changer(midi_status_t *st)
{
	GtkObject *adj;
	GtkWidget *vel;

	adj = gtk_adjustment_new(100.0, 0.0, 200.0, 10.0, 10.0, 0.0);
	st->vel_scale = 100;
	vel = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_range_set_update_policy(GTK_RANGE(vel),
				    GTK_UPDATE_DISCONTINUOUS);
	gtk_scale_set_digits(GTK_SCALE(vel), 0);
	gtk_scale_set_value_pos(GTK_SCALE(vel), GTK_POS_TOP);
	gtk_scale_set_draw_value(GTK_SCALE(vel), TRUE);
	gtk_widget_show(vel);

	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
			   GTK_SIGNAL_FUNC(adjust_velocity), st);

	return vel;
}

/*
 * velocity scale up/down
 */
static void
adjust_velocity(GtkAdjustment *adj, midi_status_t *st)
{
	if (adj->value != st->vel_scale) {
		st->vel_scale = adj->value;
		restart_notes(st);
	}
}

/*
 * check whether the port is writable
 */
static int
is_redirect(port_status_t *port)
{
	return (do_output &&
		port_num_subscription(port->port, SND_SEQ_QUERY_SUBS_READ) > 0);
}

/*
 * restart notes with new adjustment
 */
static void
restart_notes(midi_status_t *st)
{
	int i, p;

	for (p = 0; p < st->num_ports; p++) {
		port_status_t *port = &st->ports[p];
		if (is_redirect(port)) {
			for (i = 0; i < MIDI_CHANNELS; i++) {
				send_notes_off(&port->ch[i]);
				resume_notes_on(&port->ch[i]);
			}
		}
	}
}

/*
 * create viewer widget
 */
static GtkWidget *
create_viewer(port_status_t *port)
{
	GtkWidget *table;
	int i;

	table = gtk_table_new(aseqview_cols, MIDI_CHANNELS + 1, FALSE);
	for (i = 0; i < aseqview_cols - 1; i++)
		gtk_table_set_col_spacing(GTK_TABLE(table), i, 4);
	create_viewer_titles(table);
	for (i = 0; i < MIDI_CHANNELS; i++)
		create_channel_viewer(table, port, i);
	gtk_widget_show(table);

	return table;
}


/*
 * create viewer title row
 */
static void
create_viewer_titles(GtkWidget *table)
{
	GtkWidget *w;

	w = gtk_label_new("Ch");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_CHNUM, V_CHNUM+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Prog");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PROG, V_PROG+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Vel");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_VEL, V_VEL+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Main");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_MAIN, V_MAIN+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Exp");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_EXP, V_EXP+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Pan");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PAN, V_PAN+1, 0, 1);
	gtk_widget_show(w);

	w = gtk_label_new("Pitch");
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PITCH, V_PITCH+1, 0, 1);
	gtk_widget_show(w);

	if (show_piano) {
		w = gtk_label_new("Piano");
		gtk_table_attach_defaults(GTK_TABLE(table), w, V_PIANO, V_PIANO+1, 0, 1);
		gtk_widget_show(w);
	}
}


/*
 * set color of velocity bar
 */
static void
set_vel_bar_color(GtkWidget *w, int is_drum, int in_buf)
{
	if (in_buf) {
		av_ringbuf_write(VEL_COLOR, w, is_drum);
	} else {
		if (is_drum)
			channel_status_bar_set_color_rgb(w, 0xffff, 0x4000, 0x4000);
		else
			channel_status_bar_set_color_rgb(w, 0x2000, 0xb000, 0x2000);
	}
}


/*
 * create a channel
 */
static void
create_channel_viewer(GtkWidget *table, port_status_t *st, int ch)
{
	gchar tmp[9];
	int top = ch + 1, bottom = ch + 2;
	channel_status_t *chst = &st->ch[ch];
	GtkWidget *w;

	/* channel number */
	sprintf(tmp, "%d", ch);
	w = gtk_toggle_button_new_with_label(tmp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
	gtk_signal_connect(GTK_OBJECT(w), "clicked",
			   GTK_SIGNAL_FUNC(mute_channel),
			   chst);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_CHNUM, V_CHNUM + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_chnum = w;

	/* program name */
	w = gtk_label_new(chst->progname);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PROG, V_PROG + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_prog = w;

	/* velocity */
	w = level_bar_new(64, 16, 0, 127, 0);
	set_vel_bar_color(w, chst->is_drum, 0);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_VEL, V_VEL + 1,
				  top, bottom);
	level_bar_set_level_color_rgb(w, 0xffff, 0xffff, 0x4000);
	gtk_widget_show(w);
	chst->w_vel = w;

	/* main vol */
	w = solid_bar_new(48, 16, 0, 127, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0x8000, 0x8000, 0xffff);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_MAIN, V_MAIN + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_main = w;

	/* expression */
	w = solid_bar_new(48, 16, 0, 127, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0xffff, 0x6000, 0xb000);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_EXP, V_EXP + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_exp = w;

	/* pan */
	w = arrow_bar_new(36, 16, 0, 127, 64, FALSE);
	channel_status_bar_set_color_rgb(w, 0xffff, 0x8000, 0x0000);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PAN, V_PAN + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_pan = w;

	/* pitch */
	w = arrow_bar_new(36, 16, -8192, 8191, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0x6000, 0xc000, 0xc000);
	gtk_table_attach_defaults(GTK_TABLE(table), w, V_PITCH, V_PITCH + 1,
				  top, bottom);
	gtk_widget_show(w);
	chst->w_pitch = w;

	/* piano */
	if (show_piano) {
		w = piano_new(NULL);
		gtk_table_attach_defaults(GTK_TABLE(table), w, V_PIANO, V_PIANO + 1,
					  top, bottom);
		chst->w_piano = w;
		gtk_widget_show(w);
	}
	
}


/*
 * mute/unmute a channel
 */
static void
mute_channel(GtkToggleButton *w, channel_status_t *chst)
{
	if (w->active) {
		chst->mute = 1;
		if (is_redirect(chst->port))
			send_notes_off(chst);
	} else {
		chst->mute = 0;
		if (is_redirect(chst->port))
			resume_notes_on(chst);
	}
}

/*
 * stop all sounds on the given channel:
 * send ALL_NOTES_OFF control to the subscriber port
 */
static void
send_notes_off(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	int i;

	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	snd_seq_ev_set_controller(&tmpev, chst->ch, MIDI_CTL_ALL_SOUNDS_OFF, 0);
	port_write_event(chst->port->port, &tmpev, 1);
}

/*
 * stop all sounds on the given channel:
 * send ALL_NOTES_OFF control to the subscriber port
 */
static void
send_resets(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	int i;

	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	snd_seq_ev_set_controller(&tmpev, chst->ch, MIDI_CTL_RESET_CONTROLLERS, 0);
	port_write_event(chst->port->port, &tmpev, 0);
	snd_seq_ev_set_controller(&tmpev, chst->ch, MIDI_CTL_ALL_SOUNDS_OFF, 0);
	port_write_event(chst->port->port, &tmpev, 0);
	tmpev.type = SND_SEQ_EVENT_REGPARAM;
	tmpev.data.control.param = 0;
	tmpev.data.control.value = 256;
	port_write_event(chst->port->port, &tmpev, 0);
}

/*
 * start the existing notes on the channel:
 * sends note-on again of all notes
 */
static void
resume_notes_on(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	port_status_t *port = chst->port;
	int i;
	int pitch_adj = port->main->pitch_adj;
	int vel_scale = port->main->vel_scale;

	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	for (i = 0; i < NUM_KEYS; i++) {
		if (chst->vel[i]) {
			int key, vel;
			key = i;
			if (! chst->is_drum)
				key += pitch_adj;
			if (key >= 128)
				key = 127;
			vel = ((int)chst->vel[i] * vel_scale) / 100;
			if (vel >= 128)
				vel = 127;
			snd_seq_ev_set_noteon(&tmpev, chst->ch, key, vel);
			port_write_event(port->port, &tmpev, 0);
		}
	}
	port_flush_event(port->port);
}

/*
 */
static int
update_time(GtkWidget *w)
{
	midi_status_t *st = gtk_object_get_user_data(GTK_OBJECT(w));

	if (st->timer_update) {
		snd_seq_queue_status_t *qst;
		char tmp[8];

#ifdef ALSA_API_ENCAP
		snd_seq_queue_status_alloca(&qst);
#else
		qst = alloca(sizeof(snd_seq_queue_status_t));
#endif
		st->timer_update = FALSE;
		if (! snd_seq_get_queue_status(port_client_get_seq(st->client), st->queue, qst)) {
			const snd_seq_real_time_t *rt;
#ifdef ALSA_API_ENCAP
			rt = snd_seq_queue_status_get_real_time(qst);
#else
			rt = &qst->time;
#endif
			sprintf(tmp, "%02d:%02d", (int)rt->tv_sec / 60,
				(int)rt->tv_sec % 60);
			gtk_label_set_text(GTK_LABEL(st->w_time),  tmp);
		}
	}
	return TRUE;
}


/*
 * input handler from Gtk
 */
static void
handle_input(gpointer data, gint source, GdkInputCondition condition)
{
	midi_status_t *st = (midi_status_t *)data;
	port_client_do_event(st->client);
}

/*
 * read-subscription callback from portlib:
 * if the first client appears, reset MIDI
 */
static int
port_subscribed(port_t *p, int type, snd_seq_event_t *ev, port_status_t *port)
{
	if (port_num_subscription(p, SND_SEQ_QUERY_SUBS_READ) == 1)
		reset_all(port->main);
	return 0;
}

/*
 * write-unsubscription callback:
 * if all clients are left, reset MIDI
 */
static int
port_unused(port_t *p, int type, snd_seq_event_t *ev, port_status_t *port)
{
	if (port_num_subscription(p, SND_SEQ_QUERY_SUBS_WRITE) == 0)
		reset_all(port->main);
	return 0;
}

/*
 */
static void
av_channel_update(GtkWidget *w, int val, int in_buf)
{
	if (in_buf) {
		av_ringbuf_write(UPDATE_STATUS, w, val);
	} else
		channel_status_bar_update(w, val);
}

/*
 */
static void
av_note_update(GtkWidget *w, int key, int note_on, int in_buf)
{
	if (in_buf) {
		if (note_on)
			av_ringbuf_write(NOTE_ON, w, key);
		else
			av_ringbuf_write(NOTE_OFF, w, key);
	} else {
		if (note_on)
			piano_note_on(PIANO(w), key);
		else
			piano_note_off(PIANO(w), key);
	}
}

/*
 * redirect an event to subscribers port
 */
static void
redirect_event(port_status_t *port, snd_seq_event_t *ev)
{
	if (snd_seq_ev_is_channel_type(ev)) {
		/* normal MIDI events - check channel */
		if (ev->data.note.channel >= MIDI_CHANNELS)
			return;
		/* abondoned if muted */
		if (port->ch[ev->data.note.channel].mute)
			return;
	}
	snd_seq_ev_set_direct(ev);
	snd_seq_ev_set_subs(ev);
	if (snd_seq_ev_is_note_type(ev)) {
		/* modify key / velocity for note events */
		int key_saved = ev->data.note.note;
		int vel_saved = ev->data.note.velocity;
		int key, vel;
		key = ev->data.note.note;
		if (! port->ch[ev->data.note.channel].is_drum)
			key += port->main->pitch_adj;
		if (key >= 128)
			key = 127;
		vel = ((int)ev->data.note.velocity * port->main->vel_scale) / 100;
		if (vel >= 128)
			vel = 127;
		ev->data.note.note = key;
		ev->data.note.velocity = vel;
		port_write_event(port->port, ev, 0);
		ev->data.note.note = key_saved;
		ev->data.note.velocity = vel_saved;
	} else
		port_write_event(port->port, ev, 0);
}

/*
 * callback from portlib - process a received MIDI event
 */
static int
process_event(port_t *p, int type, snd_seq_event_t *ev, port_status_t *port)
{
	if (port->index < 0)
		return 0;

	port->main->queue = ev->queue;
	port->main->timer_update = TRUE;

	if (is_redirect(port))
		redirect_event(port, ev);

	switch (ev->type) {
	case SND_SEQ_EVENT_NOTEON:
	case SND_SEQ_EVENT_KEYPRESS:
		change_note(port, ev->data.note.channel, ev->data.note.note,
			    ev->data.note.velocity,
			    use_thread);
		break;
	case SND_SEQ_EVENT_NOTEOFF:
		change_note(port, ev->data.note.channel, ev->data.note.note, 0,
			    use_thread);
		break;
	case SND_SEQ_EVENT_PGMCHANGE:
		change_program(port, ev->data.control.channel, ev->data.control.value,
			       use_thread);
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		change_controller(port, ev->data.control.channel,
				  ev->data.control.param,
				  ev->data.control.value,
				  use_thread);
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		change_pitch(port, ev->data.control.channel, ev->data.control.value,
			     use_thread);
		break;
	case SND_SEQ_EVENT_SYSEX:
		parse_sysex(port, ev->data.ext.len, ev->data.ext.ptr,
			    use_thread);
		break;
	}

	return 0;
}

/*
 * change note (note-on/off, key change)
 */
static void
change_note(port_status_t *port, int ch, int key, int vel, int in_buf)
{
	channel_status_t *chst;
	int i;

	if (key < 0 || key >= NUM_KEYS)
		return;
	if (vel < 0 || vel >= MAX_MIDI_VALS)
		return;

	chst = &port->ch[ch];
	chst->vel[key] = vel;

	/* update maximum velocity */
	if (vel >= chst->max_vel) {
		chst->max_vel = vel;
		chst->max_vel_key = key;
		av_channel_update(chst->w_vel, chst->max_vel, in_buf);
	} else {
		if (chst->max_vel_key == key) {
			chst->max_vel = vel;
			for (i = 0; i < NUM_KEYS; i++) {
				if (chst->vel[i] > chst->max_vel) {
					chst->max_vel = chst->vel[i];
					chst->max_vel_key = i;
				}
			}
			av_channel_update(chst->w_vel, chst->max_vel, in_buf);
		}
	}
	if (show_piano) {
		if (chst->vel[key] > 0)
			av_note_update(chst->w_piano, key, 1, in_buf);
		else
			av_note_update(chst->w_piano, key, 0, in_buf);
	}
}

/*
 * change controller
 */
static void
change_controller(port_status_t *port, int ch, int param, int value, int in_buf)
{
	channel_status_t *chst;

	if (param < 0 || param >= MAX_MIDI_VALS)
		return;

	chst = &port->ch[ch];
	chst->ctrl[param] = value;

	switch (param) {
	case MIDI_CTL_ALL_SOUNDS_OFF:
		all_sounds_off(chst, in_buf);
		break;
	case MIDI_CTL_ALL_NOTES_OFF:
		all_notes_off(chst, in_buf);
		break;
	case MIDI_CTL_RESET_CONTROLLERS:
		reset_controllers(chst, in_buf);
		break;
	case MIDI_CTL_MSB_EXPRESSION:
		av_channel_update(chst->w_exp, value, in_buf);
		break;
	case MIDI_CTL_MSB_MAIN_VOLUME:
		av_channel_update(chst->w_main, value, in_buf);
		break;
	case MIDI_CTL_MSB_PAN:
		av_channel_update(chst->w_pan, value, in_buf);
		break;

	case MIDI_CTL_MSB_BANK:
		if (port->main->midi_mode == MIDI_MODE_XG) {
			/* change drum flag and color */
			if (value == 127)
				chst->is_drum = 1;
			else
				chst->is_drum = 0;
			set_vel_bar_color(chst->w_vel, chst->is_drum, in_buf);
		}
		break;
	}
}

/*
 * change pitch
 */
static void
change_pitch(port_status_t *port, int ch, int value, int in_buf)
{
	channel_status_t *chst;

	if (ch < 0 || ch >= MIDI_CHANNELS)
		return;
	chst = &port->ch[ch];
	chst->pitch = value;
	av_channel_update(chst->w_pitch, value, in_buf);
}

/*
 * change program
 */
static void
change_program(port_status_t *port, int ch, int prog, int in_buf)
{
	channel_status_t *chst;

	if (ch < 0 || ch >= MIDI_CHANNELS)
		return;
	chst = &port->ch[ch];
	chst->prog = prog;
	sprintf(chst->progname, "%3d", prog);
	if (in_buf) {
		av_ringbuf_write(UPDATE_PGM, chst->w_prog, (unsigned long)chst->progname);
	} else {
		gtk_label_set_text(GTK_LABEL(chst->w_prog), chst->progname);
	}
}

/*
 * clear all notes
 */
static void
all_sounds_off(channel_status_t *chst, int in_buf)
{
	memset(chst->vel, 0, sizeof(chst->vel));
	chst->max_vel = 0;
	chst->max_vel_key = 0;
	av_channel_update(chst->w_vel, 0, in_buf);
}

/*
 * note off - not exactly same as all_sounds_off
 */
static void
all_notes_off(channel_status_t *chst, int in_buf)
{
	all_sounds_off(chst, in_buf);
}

/*
 * reset controllers
 */
static void
reset_controllers(channel_status_t *chst, int in_buf)
{
	memset(chst->ctrl, 0, sizeof(chst->ctrl));
	chst->ctrl[MIDI_CTL_MSB_MAIN_VOLUME] = 100;
	av_channel_update(chst->w_main, 100, in_buf);
	chst->ctrl[MIDI_CTL_MSB_EXPRESSION] = 127;
	av_channel_update(chst->w_exp, 127, in_buf);
	chst->ctrl[MIDI_CTL_MSB_PAN] = 64;
	av_channel_update(chst->w_pan, 64, in_buf);
}

/*
 * reset all stuff
 */
static void
reset_all(midi_status_t *st)
{
	int i, p, j;

	st->midi_mode = MIDI_MODE_GM;
	st->timer_update = TRUE;
	display_midi_mode(st, 0);
	for (p = 0; p < st->num_ports; p++) {
		port_status_t *port = &st->ports[p];
		if (port->index < 0)
			continue;
		for (i = 0; i < MIDI_CHANNELS; i++) {
			channel_status_t *chst = &port->ch[i];
			all_sounds_off(chst, 0);
			reset_controllers(chst, 0);
			change_pitch(port, i, 0, 0);
			change_program(port, i, 0, 0);
			if (is_redirect(port))
				send_resets(chst);
			if (i == 9)
				chst->is_drum = 1;
			else
				chst->is_drum = 0;
			set_vel_bar_color(chst->w_vel, chst->is_drum, 0);
			if (chst->w_piano) {
				for (j = 0; j < NUM_KEYS; j++)
					piano_note_off(PIANO(chst->w_piano), j);
			}
		}
		if (is_redirect(port))
			port_flush_event(port->port);
	}
}

/*
 * convert channel parameter in GS sysex
 */
static int
get_channel(unsigned char cmd)
{
	int p = cmd & 0x0f;
	if (p == 0)
		p = 9;
	else if (p < 10)
		p--;
	return p;
}
/*
 * parse sysex message
 */
static void
parse_sysex(port_status_t *port, int len, unsigned char *buf, int in_buf)
{
	midi_status_t *st = port->main;

	/* GM on */
	static unsigned char gm_on_macro[] = {
		0x7e,0x7f,0x09,0x01,
	};
	/* XG on */
	static unsigned char xg_on_macro[] = {
		0x43,0x10,0x4c,0x00,0x00,0x7e,0x00,
	};
	/* GS prefix
	 * drum channel: XX=0x1?(channel), YY=0x15, ZZ=on/off
	 * reverb mode: XX=0x01, YY=0x30, ZZ=0-7
	 * chorus mode: XX=0x01, YY=0x38, ZZ=0-7
	 * master vol:  XX=0x00, YY=0x04, ZZ=0-127
	 */
	static unsigned char gs_pfx_macro[] = {
		0x41,0x10,0x42,0x12,0x40,/*XX,YY,ZZ*/
	};

	if (len <= 0 || buf[0] != 0xf0)
		return;
	/* skip first byte */
	buf++;
	len--;

	/* GM on */
	if (len >= sizeof(gm_on_macro) &&
	    memcmp(buf, gm_on_macro, sizeof(gm_on_macro)) == 0) {
		if (st->midi_mode != MIDI_MODE_GS &&
		    st->midi_mode != MIDI_MODE_XG) {
			st->midi_mode = MIDI_MODE_GM;
			display_midi_mode(st, in_buf);
			/*init_midi_status(chset);*/
		}
	}

	/* GS macros */
	else if (len >= 8 &&
		 memcmp(buf, gs_pfx_macro, sizeof(gs_pfx_macro)) == 0) {
		if (st->midi_mode != MIDI_MODE_GS &&
		    st->midi_mode != MIDI_MODE_XG) {
			st->midi_mode = MIDI_MODE_GS;
			display_midi_mode(st, in_buf);
		}

		if (buf[5] == 0x00 && buf[6] == 0x7f && buf[7] == 0x00) {
			/* GS reset */
			/*init_midi_status(chset);*/
		}

		else if ((buf[5] & 0xf0) == 0x10 && buf[6] == 0x15) {
			/* drum pattern */
			int p = get_channel(buf[5]);
			if (p < MIDI_CHANNELS) {
				if (buf[7])
					port->ch[p].is_drum = 1;
				else
					port->ch[p].is_drum = 0;
				set_vel_bar_color(port->ch[p].w_vel, port->ch[p].is_drum, in_buf);
			}

		} else if ((buf[5] & 0xf0) == 0x10 && buf[6] == 0x21) {
			/* program */
			int p = get_channel(buf[5]);
			if (p < MIDI_CHANNELS && ! port->ch[p].is_drum)
				change_program(port, p, buf[7], in_buf);
		}
	}

	/* XG on */
	else if (len >= sizeof(xg_on_macro) &&
		 memcmp(buf, xg_on_macro, sizeof(xg_on_macro)) == 0) {
		st->midi_mode = MIDI_MODE_XG;
		display_midi_mode(st, in_buf);
	}
}

/*
 */
#include "bitmaps/gm.xbm"
#include "bitmaps/gs.xbm"
#include "bitmaps/xg.xbm"

static GdkPixmap *
create_midi_pixmap(GtkWidget *window, char *data, int width, int height, int active)
{
	GtkStyle *style;
	GdkPixmap *pixmap;
	GdkColor tmpfg, *fg;

	style = gtk_widget_get_style(window);
	if (active) {
		alloc_color(&tmpfg, 0, 0, 0x8000);
		fg = &tmpfg;
	} else
		fg = &style->fg[GTK_STATE_INSENSITIVE];
	pixmap = gdk_pixmap_create_from_data(window->window,
					     data, width, height,
					     style->depth,
					     fg, &style->bg[GTK_STATE_NORMAL]);
	return pixmap;
}

static GtkWidget *
display_midi_init(GtkWidget *window, midi_status_t *st)
{
	GtkWidget *w, *vbox;

	st->w_gm_xpm = create_midi_pixmap(window, gm_bits, gm_width, gm_height, TRUE);
	st->w_gs_xpm = create_midi_pixmap(window, gs_bits, gs_width, gs_height, TRUE);
	st->w_xg_xpm = create_midi_pixmap(window, xg_bits, xg_width, xg_height, TRUE);
	st->w_gm_xpm_off = create_midi_pixmap(window, gm_bits, gm_width, gm_height, FALSE);
	st->w_gs_xpm_off = create_midi_pixmap(window, gs_bits, gs_width, gs_height, FALSE);
	st->w_xg_xpm_off = create_midi_pixmap(window, xg_bits, xg_width, xg_height, FALSE);

	vbox = gtk_vbox_new(FALSE, 0);

	w = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(w),
			      gm_width + gs_width + xg_width + 20,
			      gm_height);
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	gtk_object_set_user_data(GTK_OBJECT(w), st);
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
			   GTK_SIGNAL_FUNC(expose_midi_mode),
			   NULL);
	st->w_midi_mode = w;
	w = gtk_label_new("00:00");
	gtk_object_set_user_data(GTK_OBJECT(w), st);
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
	gtk_timeout_add(1000, (GtkFunction)update_time, (gpointer)w);
	gtk_widget_show(w);

	st->w_time = w;

	gtk_widget_show(vbox);

	return vbox;
}

static void
display_midi_mode(midi_status_t *st, int in_buf)
{
	if (in_buf) {
		av_ringbuf_write(UPDATE_MODE, st->w_midi_mode, 0);
	} else {
		expose_midi_mode(st->w_midi_mode);
	}
}

static int
expose_midi_mode(GtkWidget *w)
{
	midi_status_t *st = gtk_object_get_user_data(GTK_OBJECT(w));
	GdkDrawable *p;
	int width = w->allocation.width;
	int height = w->allocation.height;
	int x_ofs = (width - gm_width - gs_width - xg_width - 20) / 2;
	int y_ofs = (height - gm_height) / 2;
			  
	if (st->midi_mode == MIDI_MODE_GM)
		p = st->w_gm_xpm;
	else
		p = st->w_gm_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL],
			p, 0, 0, x_ofs, y_ofs, gm_width, gm_height);

	if (st->midi_mode == MIDI_MODE_GS)
		p = st->w_gs_xpm;
	else
		p = st->w_gs_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL],
			p, 0, 0, x_ofs + gm_width + 10, y_ofs,
			gs_width, gs_height);

	if (st->midi_mode == MIDI_MODE_XG)
		p = st->w_xg_xpm;
	else
		p = st->w_xg_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL],
			p, 0, 0, x_ofs + gm_width + gs_width + 20, y_ofs,
			xg_width, xg_height);

	return FALSE;
}
