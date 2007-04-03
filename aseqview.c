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
#include "piano.h" // From swami.
#include "portlib.h"

#define MIDI_CHANNELS	16
#define NUM_KEYS		128
#define NUM_CTRLS		128
#define MAX_MIDI_VALS	128
#define PROG_NAME_LEN	8
#define TEMPER_UNKNOWN	8

#if SND_LIB_MAJOR == 0 && SND_LIB_MINOR <= 5
#define MIDI_CTL_MSB_BANK			SND_MCTL_MSB_BANK
#define MIDI_CTL_MSB_MAIN_VOLUME	SND_MCTL_MSB_MAIN_VOLUME
#define MIDI_CTL_MSB_PAN			SND_MCTL_MSB_PAN
#define MIDI_CTL_MSB_EXPRESSION		SND_MCTL_MSB_EXPRESSION
#define MIDI_CTL_ALL_SOUNDS_OFF		SND_MCTL_ALL_SOUNDS_OFF
#define MIDI_CTL_RESET_CONTROLLERS	SND_MCTL_RESET_CONTROLLERS
#define MIDI_CTL_ALL_NOTES_OFF		SND_MCTL_ALL_NOTES_OFF
#endif

#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
#ifdef snd_seq_client_info_alloca
#define ALSA_API_ENCAP
#endif
#endif

typedef struct channel_status_t channel_status_t;
typedef struct port_status_t port_status_t;
typedef struct midi_status_t midi_status_t;

struct channel_status_t {
	port_status_t *port;
	int ch, mute, is_drum;
	char progname[PROG_NAME_LEN + 1];
	unsigned char ctrl[NUM_CTRLS];
	int temper_type;
	unsigned char vel[NUM_KEYS];
	int max_vel_key, max_vel;
	/* widgets */
	GtkWidget *w_chnum, *w_prog;
	GtkWidget *w_vel, *w_main, *w_exp;
	GtkWidget *w_pan, *w_pitch, *w_temper_type;
	GtkWidget *w_piano;
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
	int midi_mode;
	int temper_keysig;
	int timer_update, queue;
	int temper_type_mute;
	int pitch_adj, vel_scale;
	GtkWidget *w_midi_mode, *w_temper_keysig, *w_time, *w_tt_button[8];
	GdkPixmap *w_gm_xpm, *w_gs_xpm, *w_xg_xpm;
	GdkPixmap *w_gm_xpm_off, *w_gs_xpm_off, *w_xg_xpm_off;
	GdkPixmap *w_tk_xpm[32], *w_tk_xpm_adj[32], *w_tt_xpm[9];
};

enum {
	V_CHNUM = 0,
	V_PROG,
	V_VEL,
	V_MAIN,
	V_EXP,
	V_PAN,
	V_PITCH,
	V_TEMPER,
	V_PIANO,
	V_COLS
};

enum {
	UPDATE_MUTE,
	VEL_COLOR,
	UPDATE_STATUS,
	NOTE_ON,
	NOTE_OFF,
	PIANO_RESET,
	UPDATE_PGM,
	UPDATE_MODE,
	UPDATE_TEMPER_KEYSIG,
	UPDATE_TEMPER_TYPE,
	HIDE_TT_BUTTON
};

enum {
	MIDI_MODE_GM,
	MIDI_MODE_GS,
	MIDI_MODE_XG
};

/*
 * prototypes
 */
static int parse_addr(char *, int *, int *);
static void usage(void);
static midi_status_t *midi_status_new(int);
static void create_port_window(port_status_t *);
static int quit(GtkWidget *);
static GtkWidget *create_viewer(port_status_t *);
static void create_viewer_titles(GtkWidget *);
static void create_channel_viewer(GtkWidget *, port_status_t *, int);
static void mute_channel(GtkToggleButton *, channel_status_t *);
static GtkWidget *display_midi_init(GtkWidget *, midi_status_t *);
static GdkPixmap *create_midi_pixmap(GtkWidget *,
		char *, int, int, int, int *);
static int expose_midi_mode(GtkWidget *);
static int expose_temper_keysig(GtkWidget *);
static int expose_temper_type(GtkWidget *);
static int update_time(GtkWidget *);
static void suppress_temper_type(GtkToggleButton *, midi_status_t *);
static GtkWidget *create_pitch_changer(midi_status_t *);
static void adjust_pitch(GtkAdjustment *, midi_status_t *);
static GtkWidget *create_velocity_changer(midi_status_t *);
static void adjust_velocity(GtkAdjustment *, midi_status_t *);
static void restart_notes(midi_status_t *);
static void send_notes_off(channel_status_t *);
static void resume_notes_on(channel_status_t *);
static int port_subscribed(port_t *, int, snd_seq_event_t *, port_status_t *);
static int port_unused(port_t *, int, snd_seq_event_t *, port_status_t *);
static int process_event(port_t *, int, snd_seq_event_t *, port_status_t *);
static void redirect_event(port_status_t *, snd_seq_event_t *);
static void change_note(port_status_t *, int, int, int, int);
static void change_program(port_status_t *, int, int, int);
static void change_controller(port_status_t *, int, int, int, int);
static void all_sounds_off(channel_status_t *, int);
static void reset_controllers(channel_status_t *, int);
static void all_notes_off(channel_status_t *, int);
static void change_pitch(port_status_t *, int, int, int);
static void parse_sysex(port_status_t *, int, unsigned char *, int);
static int get_channel(unsigned char);
static void visualize_temper_type(midi_status_t *, int);
static void reset_all(midi_status_t *, int);
static void send_resets(channel_status_t *);
static int is_redirect(port_status_t *);
static void av_mute_update(GtkWidget *, int, int);
static void set_vel_bar_color(GtkWidget *, int, int);
static void av_channel_update(GtkWidget *, int, int);
static void av_note_update(GtkWidget *, int, int, int);
static void av_piano_reset(GtkWidget *, int);
static void av_program_update(GtkWidget *, char *, int);
static void display_midi_mode(GtkWidget *, int);
static void display_temper_keysig(GtkWidget *, int);
static void display_temper_type(GtkWidget *, int);
static void av_hide_tt_button(GtkWidget *, int, int);
static void av_ringbuf_init(void);
static int av_ringbuf_read(int *, GtkWidget **, long *);
static int av_ringbuf_write(int, GtkWidget *, long);
static void *midi_loop(void *);
static gboolean idle_cb(gpointer);
static int get_file_desc(midi_status_t *);
static void handle_input(gpointer, gint, GdkInputCondition);
static int set_realtime_priority(int);

/*
 * local common variables
 */
static int do_output = TRUE;
static int rt_prio = FALSE;
static int use_thread = TRUE;
static pthread_t midi_thread;
static int show_piano = TRUE;
static int aseqview_cols = V_COLS;

static struct option long_option[] = {
	{ "nooutput", 0, NULL, 'o' },
	{ "realtime", 0, NULL, 'r' },
	{ "source", 1, NULL, 's' },
	{ "dest", 1, NULL, 'd' },
	{ "ports", 1, NULL, 'p' },
	{ "help", 0, NULL, 'h' },
	{ "thread", 0, NULL, 't' },
	{ "nothread", 0, NULL, 'm' },
	{ "nopiano", 0, NULL, 'P' },
	{ NULL, 0, NULL, 0 }
};

/*
 * main routine
 */
int main(int argc, char **argv)
{
	int c, p;
	midi_status_t *st;
	int src_client = -1, src_port;
	int dest_client = -1, dest_port;
	int num_ports = 1;
	port_status_t *port;
	
	gtk_init(&argc, &argv);
	while ((c = getopt_long(argc, argv, "ors:d:p:tmP",
			long_option, NULL)) != -1) {
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
				return 1;
			}
			break;
		case 'd':
			if (parse_addr(optarg, &dest_client, &dest_port) < 0) {
				fprintf(stderr, "invalid argument %s for -d\n", optarg);
				return 1;
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
			break;
		default:
			usage();
			return 1;
		}
	}
	if (num_ports < 1 || num_ports > 20)
		g_error("invalid port numbers %d\n", num_ports);
	/* create instance */
	st = midi_status_new(num_ports);
	for (p = 0; p < num_ports; p++) {
		port = &st->ports[p];
		/* create window */
		create_port_window(port);
		/* add callbacks */
		port_add_callback(port->port, PORT_SUBSCRIBE_CB,
				(port_callback_t) port_subscribed, port);
		port_add_callback(port->port, PORT_UNUSE_CB,
				(port_callback_t) port_unused, port);
		port_add_callback(port->port, PORT_MIDI_EVENT_CB,
				(port_callback_t) process_event, port);
	}
	if (use_thread)
		av_ringbuf_init();
	/* explicit subscription to ports */
	if (src_client >= 0 && src_client != SND_SEQ_ADDRESS_SUBSCRIBERS)
		port_connect_from(st->ports[0].port, src_client, src_port);
	if (do_output && dest_client >= 0
			&& dest_client != SND_SEQ_ADDRESS_SUBSCRIBERS) {
		port_connect_to(st->ports[0].port, dest_client, dest_port);
		reset_all(st, 0);
	}
	if (use_thread) {
		pthread_create(&midi_thread, NULL, midi_loop, st);
		gtk_idle_add(idle_cb, st);
	} else {
		gdk_input_add(get_file_desc(st), GDK_INPUT_READ, handle_input, st);
		if (rt_prio)
			set_realtime_priority(SCHED_FIFO);
	}
	gtk_main();
	if (use_thread) {
		port_client_stop(st->client);
		pthread_join(midi_thread, NULL);
	}
	return 0;
}

/*
 * parse client:port address from command line
 */
static int parse_addr(char *arg, int *clientp, int *portp)
{
	char *p;
	
	if (isdigit(*arg)) {
		if ((p = strpbrk(arg, ":.")) == NULL)
			return -1;
		*clientp = atoi(arg);
		*portp = atoi(p + 1);
	} else {
		if (*arg != 's' && *arg != 'S')
			return -1;
		*clientp = SND_SEQ_ADDRESS_SUBSCRIBERS;
		*portp = 0;
	}
	return 0;
}

/*
 * print out usage
 */
static void usage(void)
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
static midi_status_t *midi_status_new(int num_ports)
{
	midi_status_t *st = g_malloc0(sizeof(*st));
	int mode, p, i;
	unsigned int caps;
	port_status_t *port;
	char name[32];
	channel_status_t *chst;
	
#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
	mode = (do_output) ? SND_SEQ_OPEN_DUPLEX : SND_SEQ_OPEN_INPUT;
#else
	mode = (do_output) ? SND_SEQ_OPEN : SND_SEQ_OPEN_IN;
#endif
	st->client = port_client_new("MIDI Viewer", mode, use_thread);
	caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
	if (do_output)
		caps |= SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
	st->num_ports = num_ports;
	st->ports = g_malloc0(sizeof(port_status_t) * num_ports);
	for (p = 0; p < num_ports; p++) {
		port = &st->ports[p];
		port->main = st;
		port->index = p;
		sprintf(name, "Viewer Port %d", p);
		port->port = port_attach(st->client, name, caps,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC);
		/* initialize channels */
		for (i = 0; i < MIDI_CHANNELS; i++) {
			chst = &port->ch[i];
			memset(chst, 0, sizeof(*chst));
			chst->port = port;
			chst->ch = i, chst->mute = 0;
			chst->is_drum = (i == 9) ? 1 : 0;
			sprintf(chst->progname, "%3d", 0);
			chst->ctrl[MIDI_CTL_MSB_MAIN_VOLUME] = 100;
			chst->ctrl[MIDI_CTL_MSB_PAN] = 64;
			chst->ctrl[MIDI_CTL_MSB_EXPRESSION] = 127;
		}
	}
	return st;
}

/*
 */
#include "bitmaps/gm.xbm"
#include "bitmaps/gs.xbm"
#include "bitmaps/xg.xbm"
#include "tmprbits.h"

/*
 * create main window
 */
static void create_port_window(port_status_t *port)
{
	GtkWidget *toplevel, *vbox, *vbox2, *hbox, *w;
	char name[64];
	int client = port_client_get_id(port->main->client);
	
	toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	sprintf(name, "ASeqView %d:%d", client, port->index);
	gtk_widget_set_name(toplevel, name);
	sprintf(name, "ALSA Sequencer Viewer %d:%d", client, port->index);
	gtk_window_set_title(GTK_WINDOW(toplevel), name);
	gtk_window_set_wmclass(GTK_WINDOW(toplevel), "aseqview", "ASeqView");
	gtk_signal_connect(GTK_OBJECT(toplevel), "delete_event",
			GTK_SIGNAL_FUNC(quit), NULL);
	gtk_signal_connect(GTK_OBJECT(toplevel), "destroy",
			GTK_SIGNAL_FUNC(quit), NULL);
	vbox = gtk_vbox_new(FALSE, 0);
	w = create_viewer(port);
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	if (port->index == 0) {
		/* control part */
		w = gtk_hseparator_new(); 
		gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);
		gtk_widget_show(w);
		hbox = gtk_hbox_new(FALSE, 0);
		gtk_widget_realize(toplevel);
		w = display_midi_init(toplevel, port->main);
		gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
		gtk_widget_show(w);
		w = gtk_vseparator_new(); 
		gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 0);
		gtk_widget_show(w);
		vbox2 = gtk_vbox_new(FALSE, 0);
		w = gtk_label_new("Pitch Correction");
		gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
		gtk_widget_show(w);
		w = create_pitch_changer(port->main);
		gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
		gtk_widget_show(w);
		w = gtk_hseparator_new(); 
		gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, TRUE, 0);
		gtk_widget_show(w);
		w = gtk_label_new("Velocity Scale");
		gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
		gtk_widget_show(w);
		w = create_velocity_changer(port->main);
		gtk_box_pack_start(GTK_BOX(vbox2), w, TRUE, TRUE, 0);
		gtk_widget_show(w);
		gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);
		gtk_widget_show(vbox2);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
	}
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(toplevel), vbox);
	gtk_widget_show(toplevel);
}

/*
 */
static int quit(GtkWidget *w)
{
	gtk_exit(0);
	return FALSE;
}

/*
 * create viewer widget
 */
static GtkWidget *create_viewer(port_status_t *port)
{
	GtkWidget *table;
	int i;
	
	table = gtk_table_new(aseqview_cols, MIDI_CHANNELS + 1, FALSE);
	for (i = 0; i < aseqview_cols - 1; i++)
		gtk_table_set_col_spacing(GTK_TABLE(table), i, 4);
	create_viewer_titles(table);
	for (i = 0; i < MIDI_CHANNELS; i++)
		create_channel_viewer(table, port, i);
	return table;
}

/*
 * create viewer title row
 */
static void create_viewer_titles(GtkWidget *table)
{
	GtkWidget *w;
	GtkTable *tbl = GTK_TABLE(table);
	
	w = gtk_label_new("Ch");
	gtk_table_attach_defaults(tbl, w, V_CHNUM, V_CHNUM + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Prog");
	gtk_table_attach_defaults(tbl, w, V_PROG, V_PROG + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Vel");
	gtk_table_attach_defaults(tbl, w, V_VEL, V_VEL + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Main");
	gtk_table_attach_defaults(tbl, w, V_MAIN, V_MAIN + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Exp");
	gtk_table_attach_defaults(tbl, w, V_EXP, V_EXP + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Pan");
	gtk_table_attach_defaults(tbl, w, V_PAN, V_PAN + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("Pitch");
	gtk_table_attach_defaults(tbl, w, V_PITCH, V_PITCH + 1, 0, 1);
	gtk_widget_show(w);
	w = gtk_label_new("");
	gtk_table_attach_defaults(tbl, w, V_TEMPER, V_TEMPER + 1, 0, 1);
	gtk_widget_show(w);
	if (show_piano) {
		w = gtk_label_new("Piano");
		gtk_table_attach_defaults(tbl, w, V_PIANO, V_PIANO + 1, 0, 1);
		gtk_widget_show(w);
	}
}

/*
 * create a channel
 */
static void create_channel_viewer(GtkWidget *table, port_status_t *st, int ch)
{
	gchar tmp[9];
	GtkWidget *w;
	channel_status_t *chst = &st->ch[ch];
	GtkTable *tbl = GTK_TABLE(table);
	int top = ch + 1, bottom = ch + 2;
	
	/* channel number */
	sprintf(tmp, "%d", ch);
	w = chst->w_chnum = gtk_toggle_button_new_with_label(tmp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
	gtk_signal_connect(GTK_OBJECT(w), "clicked",
			GTK_SIGNAL_FUNC(mute_channel), chst);
	gtk_table_attach_defaults(tbl, w, V_CHNUM, V_CHNUM + 1, top, bottom);
	gtk_widget_show(w);
	/* program name */
	w = chst->w_prog = gtk_label_new(chst->progname);
	gtk_table_attach_defaults(tbl, w, V_PROG, V_PROG + 1, top, bottom);
	gtk_widget_show(w);
	/* velocity */
	w = chst->w_vel = level_bar_new(64, 16, 0, 127, 0);
	set_vel_bar_color(w, chst->is_drum, 0);
	level_bar_set_level_color_rgb(w, 0xffff, 0xffff, 0x4000);
	gtk_table_attach_defaults(tbl, w, V_VEL, V_VEL + 1, top, bottom);
	gtk_widget_show(w);
	/* main vol */
	w = chst->w_main = solid_bar_new(48, 16, 0, 127, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0x8000, 0x8000, 0xffff);
	gtk_table_attach_defaults(tbl, w, V_MAIN, V_MAIN + 1, top, bottom);
	gtk_widget_show(w);
	/* expression */
	w = chst->w_exp = solid_bar_new(48, 16, 0, 127, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0xffff, 0x6000, 0xb000);
	gtk_table_attach_defaults(tbl, w, V_EXP, V_EXP + 1, top, bottom);
	gtk_widget_show(w);
	/* pan */
	w = chst->w_pan = arrow_bar_new(36, 16, 0, 127, 64, FALSE);
	channel_status_bar_set_color_rgb(w, 0xffff, 0x8000, 0x0000);
	gtk_table_attach_defaults(tbl, w, V_PAN, V_PAN + 1, top, bottom);
	gtk_widget_show(w);
	/* pitch */
	w = chst->w_pitch = arrow_bar_new(36, 16, -8192, 8191, 0, FALSE);
	channel_status_bar_set_color_rgb(w, 0x6000, 0xc000, 0xc000);
	gtk_table_attach_defaults(tbl, w, V_PITCH, V_PITCH + 1, top, bottom);
	gtk_widget_show(w);
	/* temper type */
	w = chst->w_temper_type = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(w), tt_width, tt_height);
	gtk_object_set_user_data(GTK_OBJECT(w), chst);
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
			GTK_SIGNAL_FUNC(expose_temper_type), NULL);
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	gtk_table_attach_defaults(tbl, w, V_TEMPER, V_TEMPER + 1, top, bottom);
	gtk_widget_show(w);
	/* piano */
	if (show_piano) {
		w = chst->w_piano = piano_new(NULL);
		gtk_table_attach_defaults(tbl, w, V_PIANO, V_PIANO + 1, top, bottom);
		gtk_widget_show(w);
	}
}

/*
 * mute/unmute a channel
 */
static void mute_channel(GtkToggleButton *w, channel_status_t *chst)
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
 */
static GtkWidget *display_midi_init(GtkWidget *window, midi_status_t *st)
{
	int i;
	GtkWidget *vbox, *hbox, *w, *table;
	gchar *tmp[] = { "eq.", "Py.", "mt.", "pu.", "u0", "u1", "u2", "u3" };
	
	st->midi_mode = MIDI_MODE_GM;
	st->temper_keysig = TEMPER_UNKNOWN;
	st->temper_type_mute = 0;
	st->w_gm_xpm = create_midi_pixmap(window,
			gm_bits, gm_width, gm_height, 1, NULL);
	st->w_gs_xpm = create_midi_pixmap(window,
			gs_bits, gs_width, gs_height, 1, NULL);
	st->w_xg_xpm = create_midi_pixmap(window,
			xg_bits, xg_width, xg_height, 1, NULL);
	st->w_gm_xpm_off = create_midi_pixmap(window,
			gm_bits, gm_width, gm_height, 0, NULL);
	st->w_gs_xpm_off = create_midi_pixmap(window,
			gs_bits, gs_width, gs_height, 0, NULL);
	st->w_xg_xpm_off = create_midi_pixmap(window,
			xg_bits, xg_width, xg_height, 0, NULL);
	for (i = 0; i < 32; i++) {
		st->w_tk_xpm[i] = create_midi_pixmap(window,
				tk_bits[i], tk_width, tk_height, 1, NULL);
		st->w_tk_xpm_adj[i] = create_midi_pixmap(window,
				tk_bits[i], tk_width, tk_height, 0, NULL);
	}
	for (i = 0; i < 9; i++)
		st->w_tt_xpm[i] = create_midi_pixmap(window,
				tt_bits[i], tt_width, tt_height, 2, tt_rgb[i]);
	vbox = gtk_vbox_new(FALSE, 0);
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_border_width(GTK_CONTAINER(hbox), 10);
	w = st->w_midi_mode = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(w),
			gm_width + gs_width + xg_width + 20, gm_height);
	gtk_object_set_user_data(GTK_OBJECT(w), st);
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
			GTK_SIGNAL_FUNC(expose_midi_mode), NULL);
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	w = st->w_temper_keysig = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(w), tk_width + 10, tk_height);
	gtk_object_set_user_data(GTK_OBJECT(w), st);
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
			GTK_SIGNAL_FUNC(expose_temper_keysig), NULL);
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_border_width(GTK_CONTAINER(hbox), 10);
	w = st->w_time = gtk_label_new("00:00");
	gtk_object_set_user_data(GTK_OBJECT(w), st);
	gtk_timeout_add(1000, (GtkFunction) update_time, (gpointer) w);
	gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
	gtk_widget_show(w);
	table = gtk_table_new(4, 2, FALSE);
	for (i = 0; i < 8; i++) {
		w = st->w_tt_button[i] = gtk_toggle_button_new_with_label(tmp[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(w), TRUE);
		gtk_signal_connect(GTK_OBJECT(w), "clicked",
				GTK_SIGNAL_FUNC(suppress_temper_type), st);
		gtk_table_attach_defaults(GTK_TABLE(table),
				w, i % 4, i % 4 + 1, i / 4, i / 4 + 1);
		gtk_widget_show(w);
	}
	gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 0);
	gtk_widget_show(table);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	return vbox;
}

/*
 */
static GdkPixmap *create_midi_pixmap(GtkWidget *window,
		char *data, int width, int height, int col_style, int *rgb)
{
	GtkStyle *style;
	GdkColor tmpfg, *fg, *bg;
	GdkPixmap *pixmap;
	
	style = gtk_widget_get_style(window);
	switch (col_style) {
	case 0:
		fg = &style->fg[GTK_STATE_INSENSITIVE];
		bg = &style->bg[GTK_STATE_NORMAL];
		break;
	case 1:
		alloc_color(&tmpfg, 0, 0, 0x8000), fg = &tmpfg;
		bg = &style->bg[GTK_STATE_NORMAL];
		break;
	case 2:
		alloc_color(&tmpfg, rgb[0], rgb[1], rgb[2]), fg = &tmpfg;
		bg = &style->black;
		break;
	}
	pixmap = gdk_pixmap_create_from_data(window->window,
			data, width, height, style->depth, fg, bg);
	return pixmap;
}

/*
 */
static int expose_midi_mode(GtkWidget *w)
{
	GdkDrawable *p;
	midi_status_t *st = gtk_object_get_user_data(GTK_OBJECT(w));
	int width = w->allocation.width;
	int height = w->allocation.height;
	int x_ofs = (width - gm_width - gs_width - xg_width - 20) / 2;
	int y_ofs = (height - gm_height) / 2;
	
	p = (st->midi_mode == MIDI_MODE_GM) ? st->w_gm_xpm : st->w_gm_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL], p, 0, 0,
			x_ofs, y_ofs, gm_width, gm_height);
	p = (st->midi_mode == MIDI_MODE_GS) ? st->w_gs_xpm : st->w_gs_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL], p, 0, 0,
			x_ofs + gm_width + 10, y_ofs, gs_width, gs_height);
	p = (st->midi_mode == MIDI_MODE_XG) ? st->w_xg_xpm : st->w_xg_xpm_off;
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL], p, 0, 0,
			x_ofs + gm_width + gs_width + 20, y_ofs, xg_width, xg_height);
	return FALSE;
}

/*
 */
static int expose_temper_keysig(GtkWidget *w)
{
	midi_status_t *st = gtk_object_get_user_data(GTK_OBJECT(w));
	int tk = st->temper_keysig, i, adj;
	GdkDrawable *p;
	int width = w->allocation.width;
	int height = w->allocation.height;
	int x_ofs = (width - tk_width - 10) / 2;
	int y_ofs = (height - tk_height) / 2;
	
	i = (tk == TEMPER_UNKNOWN) ? 0 : (tk + 8) % 32;
	adj = (tk == TEMPER_UNKNOWN) ? 0 : tk + 8 & 0x20;
	p = (adj) ? st->w_tk_xpm_adj[i] : st->w_tk_xpm[i];
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL], p, 0, 0,
			x_ofs, y_ofs, tk_width, tk_height);
	return FALSE;
}

/*
 */
static int expose_temper_type(GtkWidget *w)
{
	channel_status_t *chst = gtk_object_get_user_data(GTK_OBJECT(w));
	midi_status_t *st = chst->port->main;
	int tk = st->temper_keysig, tt = chst->temper_type, i;
	GdkDrawable *p;
	int width = w->allocation.width;
	int height = w->allocation.height;
	int x_ofs = (width - tt_width) / 2;
	int y_ofs = (height - tt_height - 6) / 2;
	
	i = (tk == TEMPER_UNKNOWN) ? 0 : tt - ((tt >= 0x40) ? 0x3c : 0) + 1;
	p = st->w_tt_xpm[i];
	gdk_draw_pixmap(w->window, w->style->fg_gc[GTK_STATE_NORMAL], p, 0, 0,
			x_ofs, y_ofs, tt_width, tt_height);
	return FALSE;
}

/*
 */
static int update_time(GtkWidget *w)
{
	midi_status_t *st = gtk_object_get_user_data(GTK_OBJECT(w));
	snd_seq_queue_status_t *qst;
	char tmp[8];
	const snd_seq_real_time_t *rt;
	
	if (st->timer_update) {
#ifdef ALSA_API_ENCAP
		snd_seq_queue_status_alloca(&qst);
#else
		qst = alloca(sizeof(snd_seq_queue_status_t));
#endif
		st->timer_update = FALSE;
		if (!snd_seq_get_queue_status(port_client_get_seq(st->client),
				st->queue, qst)) {
#ifdef ALSA_API_ENCAP
			rt = snd_seq_queue_status_get_real_time(qst);
#else
			rt = &qst->time;
#endif
			sprintf(tmp, "%02d:%02d",
					(int) rt->tv_sec / 60, (int) rt->tv_sec % 60);
			gtk_label_set_text(GTK_LABEL(st->w_time), tmp);
		}
	}
	return TRUE;
}

/*
 */
static void suppress_temper_type(GtkToggleButton *w, midi_status_t *st)
{
	int i, p;
	port_status_t *port;
	channel_status_t *chst;
	
	for (i = 0; i < 8; i++)
		if (w == GTK_TOGGLE_BUTTON(st->w_tt_button[i]))
			st->temper_type_mute ^= 1 << i;
	for (p = 0; p < st->num_ports; p++) {
		if ((port = &st->ports[p])->index < 0)
			continue;
		for (i = 0; i < MIDI_CHANNELS; i++) {
			chst = &port->ch[i];
			av_mute_update(chst->w_chnum,
					st->temper_type_mute & 1 << chst->temper_type
					- ((chst->temper_type >= 0x40) ? 0x3c : 0), use_thread);
		}
	}
}

/*
 * create pitch changer
 */
static GtkWidget *create_pitch_changer(midi_status_t *st)
{
	GtkObject *adj;
	GtkWidget *pitch;
	
	st->pitch_adj = 0;
	adj = gtk_adjustment_new(0.0, -12.0, 12.0, 1.0, 1.0, 0.0);
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
			GTK_SIGNAL_FUNC(adjust_pitch), st);
	pitch = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_range_set_update_policy(GTK_RANGE(pitch), GTK_UPDATE_DISCONTINUOUS);
	gtk_scale_set_digits(GTK_SCALE(pitch), 0);
	gtk_scale_set_value_pos(GTK_SCALE(pitch), GTK_POS_TOP);
	gtk_scale_set_draw_value(GTK_SCALE(pitch), TRUE);
	return pitch;
}

/*
 * pitch up/down
 */
static void adjust_pitch(GtkAdjustment *adj, midi_status_t *st)
{
	if (adj->value != st->pitch_adj) {
		st->pitch_adj = adj->value;
		restart_notes(st);
	}
}

/*
 * create velocity changer
 */
static GtkWidget *create_velocity_changer(midi_status_t *st)
{
	GtkObject *adj;
	GtkWidget *vel;
	
	st->vel_scale = 100;
	adj = gtk_adjustment_new(100.0, 0.0, 200.0, 10.0, 10.0, 0.0);
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
			GTK_SIGNAL_FUNC(adjust_velocity), st);
	vel = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_range_set_update_policy(GTK_RANGE(vel), GTK_UPDATE_DISCONTINUOUS);
	gtk_scale_set_digits(GTK_SCALE(vel), 0);
	gtk_scale_set_value_pos(GTK_SCALE(vel), GTK_POS_TOP);
	gtk_scale_set_draw_value(GTK_SCALE(vel), TRUE);
	return vel;
}

/*
 * velocity scale up/down
 */
static void adjust_velocity(GtkAdjustment *adj, midi_status_t *st)
{
	if (adj->value != st->vel_scale) {
		st->vel_scale = adj->value;
		restart_notes(st);
	}
}

/*
 * restart notes with new adjustment
 */
static void restart_notes(midi_status_t *st)
{
	int p, i;
	port_status_t *port;
	
	for (p = 0; p < st->num_ports; p++)
		if (is_redirect(port = &st->ports[p]))
			for (i = 0; i < MIDI_CHANNELS; i++) {
				send_notes_off(&port->ch[i]);
				resume_notes_on(&port->ch[i]);
			}
}

/*
 * stop all sounds on the given channel:
 * send ALL_NOTES_OFF control to the subscriber port
 */
static void send_notes_off(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	
	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	snd_seq_ev_set_controller(&tmpev, chst->ch, MIDI_CTL_ALL_SOUNDS_OFF, 0);
	port_write_event(chst->port->port, &tmpev, 1);
}

/*
 * start the existing notes on the channel:
 * sends note-on again of all notes
 */
static void resume_notes_on(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	int i, key, vel;
	port_status_t *port = chst->port;
	int pitch_adj = port->main->pitch_adj;
	int vel_scale = port->main->vel_scale;
	
	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	for (i = 0; i < NUM_KEYS; i++) {
		if (chst->vel[i]) {
			key = i;
			if (!chst->is_drum)
				key += pitch_adj;
			if (key >= 128)
				key = 127;
			vel = ((int) chst->vel[i] * vel_scale) / 100;
			if (vel >= 128)
				vel = 127;
			snd_seq_ev_set_noteon(&tmpev, chst->ch, key, vel);
			port_write_event(port->port, &tmpev, 0);
		}
	}
	port_flush_event(port->port);
}

/*
 * read-subscription callback from portlib:
 * if the first client appears, reset MIDI
 */
static int port_subscribed(port_t *p,
		int type, snd_seq_event_t *ev, port_status_t *port)
{
	if (port_num_subscription(p, SND_SEQ_QUERY_SUBS_READ) == 1)
		reset_all(port->main, use_thread);
	return 0;
}

/*
 * write-unsubscription callback:
 * if all clients are left, reset MIDI
 */
static int port_unused(port_t *p,
		int type, snd_seq_event_t *ev, port_status_t *port)
{
	if (port_num_subscription(p, SND_SEQ_QUERY_SUBS_WRITE) == 0)
		reset_all(port->main, use_thread);
	return 0;
}

/*
 * callback from portlib - process a received MIDI event
 */
static int process_event(port_t *p,
		int type, snd_seq_event_t *ev, port_status_t *port)
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
		change_note(port, ev->data.note.channel,
				ev->data.note.note, ev->data.note.velocity, use_thread);
		break;
	case SND_SEQ_EVENT_NOTEOFF:
		change_note(port, ev->data.note.channel,
				ev->data.note.note, 0, use_thread);
		break;
	case SND_SEQ_EVENT_PGMCHANGE:
		change_program(port, ev->data.control.channel,
				ev->data.control.value, use_thread);
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		change_controller(port, ev->data.control.channel,
				ev->data.control.param, ev->data.control.value, use_thread);
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		change_pitch(port, ev->data.control.channel,
				ev->data.control.value, use_thread);
		break;
	case SND_SEQ_EVENT_SYSEX:
		parse_sysex(port, ev->data.ext.len,
				ev->data.ext.ptr, use_thread);
		break;
	}
	return 0;
}

/*
 * redirect an event to subscribers port
 */
static void redirect_event(port_status_t *port, snd_seq_event_t *ev)
{
	int key, vel, key_saved, vel_saved;
	
	/* normal MIDI events - check channel */
	if (snd_seq_ev_is_channel_type(ev)
			&& ev->data.note.channel >= MIDI_CHANNELS)
		return;
	if (snd_seq_ev_is_note_type(ev)) {
		/* abandoned if muted */
		if (port->ch[ev->data.note.channel].mute)
			return;
		/* modify key / velocity for note events */
		key_saved = ev->data.note.note;
		vel_saved = ev->data.note.velocity;
		key = ev->data.note.note;
		if (!port->ch[ev->data.note.channel].is_drum)
			key += port->main->pitch_adj;
		if (key >= 128)
			key = 127;
		vel = ((int) ev->data.note.velocity * port->main->vel_scale) / 100;
		if (vel >= 128)
			vel = 127;
		ev->data.note.note = key;
		ev->data.note.velocity = vel;
		snd_seq_ev_set_direct(ev);
		snd_seq_ev_set_subs(ev);
		port_write_event(port->port, ev, 0);
		ev->data.note.note = key_saved;
		ev->data.note.velocity = vel_saved;
	} else {
		snd_seq_ev_set_direct(ev);
		snd_seq_ev_set_subs(ev);
		port_write_event(port->port, ev, 0);
	}
}

/*
 * change note (note-on/off, key change)
 */
static void change_note(port_status_t *port,
		int ch, int key, int vel, int in_buf)
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
		chst->max_vel_key = key, chst->max_vel = vel;
		av_channel_update(chst->w_vel, chst->max_vel, in_buf);
	} else {
		if (chst->max_vel_key == key) {
			chst->max_vel = vel;
			for (i = 0; i < NUM_KEYS; i++)
				if (chst->vel[i] > chst->max_vel) {
					chst->max_vel = chst->vel[i];
					chst->max_vel_key = i;
				}
			av_channel_update(chst->w_vel, chst->max_vel, in_buf);
		}
	}
	if (show_piano)
		av_note_update(chst->w_piano, key, (chst->vel[key] > 0), in_buf);
}

/*
 * change program
 */
static void change_program(port_status_t *port, int ch, int prog, int in_buf)
{
	channel_status_t *chst;
	
	if (ch < 0 || ch >= MIDI_CHANNELS)
		return;
	chst = &port->ch[ch];
	sprintf(chst->progname, "%3d", prog);
	av_program_update(chst->w_prog, chst->progname, in_buf);
}

/*
 * change controller
 */
static void change_controller(port_status_t *port,
		int ch, int param, int value, int in_buf)
{
	channel_status_t *chst;
	
	if (param < 0 || param >= MAX_MIDI_VALS)
		return;
	chst = &port->ch[ch];
	chst->ctrl[param] = value;
	switch (param) {
	case MIDI_CTL_MSB_BANK:
		if (port->main->midi_mode == MIDI_MODE_XG) {
			/* change drum flag and color */
			chst->is_drum = (value == 127) ? 1 : 0;
			set_vel_bar_color(chst->w_vel, chst->is_drum, in_buf);
		}
		break;
	case MIDI_CTL_MSB_MAIN_VOLUME:
		av_channel_update(chst->w_main, value, in_buf);
		break;
	case MIDI_CTL_MSB_PAN:
		av_channel_update(chst->w_pan, value, in_buf);
		break;
	case MIDI_CTL_MSB_EXPRESSION:
		av_channel_update(chst->w_exp, value, in_buf);
		break;
	case MIDI_CTL_ALL_SOUNDS_OFF:
		all_sounds_off(chst, in_buf);
		break;
	case MIDI_CTL_RESET_CONTROLLERS:
		reset_controllers(chst, in_buf);
		break;
	case MIDI_CTL_ALL_NOTES_OFF:
		all_notes_off(chst, in_buf);
		break;
	}
}

/*
 * clear all notes
 */
static void all_sounds_off(channel_status_t *chst, int in_buf)
{
	memset(chst->vel, 0, sizeof(chst->vel));
	chst->max_vel_key = chst->max_vel = 0;
	av_channel_update(chst->w_vel, 0, in_buf);
}

/*
 * reset controllers
 */
static void reset_controllers(channel_status_t *chst, int in_buf)
{
	memset(chst->ctrl, 0, sizeof(chst->ctrl));
	chst->ctrl[MIDI_CTL_MSB_MAIN_VOLUME] = 100;
	av_channel_update(chst->w_main, 100, in_buf);
	chst->ctrl[MIDI_CTL_MSB_PAN] = 64;
	av_channel_update(chst->w_pan, 64, in_buf);
	chst->ctrl[MIDI_CTL_MSB_EXPRESSION] = 127;
	av_channel_update(chst->w_exp, 127, in_buf);
}

/*
 * note off - not exactly same as all_sounds_off
 */
static void all_notes_off(channel_status_t *chst, int in_buf)
{
	all_sounds_off(chst, in_buf);
}

/*
 * change pitch
 */
static void change_pitch(port_status_t *port, int ch, int value, int in_buf)
{
	channel_status_t *chst;

	if (ch < 0 || ch >= MIDI_CHANNELS)
		return;
	chst = &port->ch[ch];
	av_channel_update(chst->w_pitch, value, in_buf);
}

/*
 * parse sysex message
 */
static void parse_sysex(port_status_t *port,
		int len, unsigned char *buf, int in_buf)
{
	/* GM on */
	static unsigned char gm_on_macro[] = {
		0x7e, 0x7f, 0x09, 0x01
	};
	/* GS prefix
	 * master vol:   XX=0x00, YY=0x04, ZZ=0-127
	 * reverb mode:  XX=0x01, YY=0x30, ZZ=0-7
	 * chorus mode:  XX=0x01, YY=0x38, ZZ=0-7
	 * drum channel: XX=0x1?, YY=0x15, ZZ=on/off (?=channel)
	 */
	static unsigned char gs_pfx_macro[] = {
		0x41, 0x10, 0x42, 0x12, 0x40 /* XX, YY, ZZ */
	};
	/* XG on */
	static unsigned char xg_on_macro[] = {
		0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00
	};
	midi_status_t *st = port->main;
	int p, need_visualize = FALSE, tt, i;
	channel_status_t *chst;
	
	if (len <= 0 || buf[0] != 0xf0)
		return;
	/* skip first byte */
	buf++, len--;
	/* GM on */
	if (len >= sizeof(gm_on_macro)
			&& memcmp(buf, gm_on_macro, sizeof(gm_on_macro)) == 0) {
		if (st->midi_mode != MIDI_MODE_GS && st->midi_mode != MIDI_MODE_XG) {
			st->midi_mode = MIDI_MODE_GM;
			display_midi_mode(st->w_midi_mode, in_buf);
		}
	/* GS macros */
	} else if (len >= 8
			&& memcmp(buf, gs_pfx_macro, sizeof(gs_pfx_macro)) == 0) {
		if (st->midi_mode != MIDI_MODE_GS && st->midi_mode != MIDI_MODE_XG) {
			st->midi_mode = MIDI_MODE_GS;
			display_midi_mode(st->w_midi_mode, in_buf);
		}
		/* GS reset */
		if (buf[5] == 0x00 && buf[6] == 0x7f && buf[7] == 0x00) {
			;
		/* drum pattern */
		} else if ((buf[5] & 0xf0) == 0x10 && buf[6] == 0x15) {
			if ((p = get_channel(buf[5])) < MIDI_CHANNELS) {
				port->ch[p].is_drum = (buf[7]) ? 1 : 0;
				set_vel_bar_color(port->ch[p].w_vel,
						port->ch[p].is_drum, in_buf);
			}
		/* program */
		} else if ((buf[5] & 0xf0) == 0x10 && buf[6] == 0x21) {
			if ((p = get_channel(buf[5])) < MIDI_CHANNELS
					&& !port->ch[p].is_drum)
				change_program(port, p, buf[7], in_buf);
		}
	/* XG on */
	} else if (len >= sizeof(xg_on_macro)
			&& memcmp(buf, xg_on_macro, sizeof(xg_on_macro)) == 0) {
		st->midi_mode = MIDI_MODE_XG;
		display_midi_mode(st->w_midi_mode, in_buf);
	/* MIDI Tuning Standard */
	} else if (len >= 7 && buf[0] >= 0x7e && buf[2] == 0x08)
		switch (buf[3]) {
		case 0x0a:
			if (st->temper_keysig == TEMPER_UNKNOWN)
				need_visualize = TRUE;
			st->temper_keysig = buf[4] - 0x40 + buf[5] * 16;
			display_temper_keysig(st->w_temper_keysig, in_buf);
			if (need_visualize)
				visualize_temper_type(st, in_buf);
			break;
		case 0x0b:
			tt = (buf[4] & 0x03) << 14 | buf[5] << 7 | buf[6];
			port = &st->ports[port->index | buf[4] >> 2];
			if (port->index >= 0 && port->index < st->num_ports)
				for (i = 0; i < MIDI_CHANNELS; i++)
					if (tt & 1 << i) {
						chst = &port->ch[i];
						chst->temper_type = buf[7];
						display_temper_type(chst->w_temper_type, in_buf);
						if (st->temper_type_mute)
							av_mute_update(chst->w_chnum,
									st->temper_type_mute & 1 << buf[7]
									- ((buf[7] >= 0x40) ? 0x3c : 0), in_buf);
					}
			break;
		}
}

/*
 * convert channel parameter in GS sysex
 */
static int get_channel(unsigned char cmd)
{
	int p = cmd & 0x0f;
	
	return (p == 0) ? 9 : ((p < 10) ? p - 1 : p);
}

/*
 */
static void visualize_temper_type(midi_status_t *st, int in_buf)
{
	int p, i;
	port_status_t *port;
	channel_status_t *chst;
	
	for (p = 0; p < st->num_ports; p++) {
		if ((port = &st->ports[p])->index < 0)
			continue;
		for (i = 0; i < MIDI_CHANNELS; i++) {
			chst = &port->ch[i];
			display_temper_type(chst->w_temper_type, in_buf);
		}
	}
	for (i = 0; i < 8; i++)
		av_hide_tt_button(st->w_tt_button[i], FALSE, in_buf);
}

/*
 * reset all stuff
 */
static void reset_all(midi_status_t *st, int in_buf)
{
	int p, i;
	port_status_t *port;
	channel_status_t *chst;
	
	for (p = 0; p < st->num_ports; p++) {
		if ((port = &st->ports[p])->index < 0)
			continue;
		for (i = 0; i < MIDI_CHANNELS; i++) {
			chst = &port->ch[i];
			if (st->temper_type_mute)
				av_mute_update(chst->w_chnum,
						st->temper_type_mute & 1, in_buf);
			all_sounds_off(chst, 0);
			chst->is_drum = (i == 9) ? 1 : 0;
			set_vel_bar_color(chst->w_vel, chst->is_drum, in_buf);
			change_program(port, i, 0, in_buf);
			reset_controllers(chst, in_buf);
			change_pitch(port, i, 0, in_buf);
			chst->temper_type = 0;
			display_temper_type(chst->w_temper_type, in_buf);
			if (show_piano)
				av_piano_reset(chst->w_piano, in_buf);
			if (is_redirect(port))
				send_resets(chst);
		}
		if (is_redirect(port))
			port_flush_event(port->port);
	}
	st->midi_mode = MIDI_MODE_GM;
	display_midi_mode(st->w_midi_mode, 0);
	st->temper_keysig = TEMPER_UNKNOWN;
	display_temper_keysig(st->w_temper_keysig, 0);
	st->timer_update = TRUE;
	for (i = 0; i < 8; i++)
		av_hide_tt_button(st->w_tt_button[i], TRUE, in_buf);
}

/*
 * stop all sounds on the given channel:
 * send ALL_NOTES_OFF control to the subscriber port
 */
static void send_resets(channel_status_t *chst)
{
	snd_seq_event_t tmpev;
	
	snd_seq_ev_clear(&tmpev);
	snd_seq_ev_set_direct(&tmpev);
	snd_seq_ev_set_subs(&tmpev);
	snd_seq_ev_set_controller(&tmpev, chst->ch,
			MIDI_CTL_RESET_CONTROLLERS, 0);
	port_write_event(chst->port->port, &tmpev, 0);
	snd_seq_ev_set_controller(&tmpev, chst->ch,
			MIDI_CTL_ALL_SOUNDS_OFF, 0);
	port_write_event(chst->port->port, &tmpev, 0);
	tmpev.type = SND_SEQ_EVENT_REGPARAM;
	tmpev.data.control.param = 0;
	tmpev.data.control.value = 256;
	port_write_event(chst->port->port, &tmpev, 0);
}

/*
 * check whether the port is writable
 */
static int is_redirect(port_status_t *port)
{
	return (do_output && port_num_subscription(port->port,
			SND_SEQ_QUERY_SUBS_READ) > 0);
}

/*
 */
static void av_mute_update(GtkWidget *w, int is_mute, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_MUTE, w, is_mute);
	else
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), is_mute);
}

/*
 * set color of velocity bar
 */
static void set_vel_bar_color(GtkWidget *w, int is_drum, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(VEL_COLOR, w, is_drum);
	else {
		if (is_drum)
			channel_status_bar_set_color_rgb(w, 0xffff, 0x4000, 0x4000);
		else
			channel_status_bar_set_color_rgb(w, 0x2000, 0xb000, 0x2000);
	}
}

/*
 */
static void av_channel_update(GtkWidget *w, int val, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_STATUS, w, val);
	else
		channel_status_bar_update(w, val);
}

/*
 */
static void av_note_update(GtkWidget *w, int key, int note_on, int in_buf)
{
	if (in_buf)
		av_ringbuf_write((note_on) ? NOTE_ON : NOTE_OFF, w, key);
	else {
		if (note_on)
			piano_note_on(PIANO(w), key);
		else
			piano_note_off(PIANO(w), key);
	}
}

/*
 */
static void av_piano_reset(GtkWidget *w, int in_buf)
{
	int i;
	
	if (in_buf)
		av_ringbuf_write(PIANO_RESET, w, 0);
	else
		for (i = 0; i < NUM_KEYS; i++)
			av_note_update(w, i, FALSE, 0);
}

/*
 */
static void av_program_update(GtkWidget *w, char *progname, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_PGM, w, (unsigned long) progname);
	else
		gtk_label_set_text(GTK_LABEL(w), progname);
}

/*
 */
static void display_midi_mode(GtkWidget *w, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_MODE, w, 0);
	else
		expose_midi_mode(w);
}

/*
 */
static void display_temper_keysig(GtkWidget *w, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_TEMPER_KEYSIG, w, 0);
	else
		expose_temper_keysig(w);
}

/*
 */
static void display_temper_type(GtkWidget *w, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(UPDATE_TEMPER_TYPE, w, 0);
	else
		expose_temper_type(w);
}

/*
 */
static void av_hide_tt_button(GtkWidget *w, int is_hide, int in_buf)
{
	if (in_buf)
		av_ringbuf_write(HIDE_TT_BUTTON, w, is_hide);
	else
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(w), is_hide);
}

/*
 */
struct av_ringbuf {
	int type;
	GtkWidget *w;
	long data;
};

#define RINGBUF_SIZE 512

static struct av_ringbuf *ringbuf;
static int ringbuf_rdptr, ringbuf_wrptr;

/*
 */
static void av_ringbuf_init(void)
{
	ringbuf = (struct av_ringbuf *) malloc(sizeof(struct av_ringbuf)
			* RINGBUF_SIZE);
	if (!ringbuf) {
		fprintf(stderr, "cannot allocate ringbuffer\n");
		exit(1);
	}
	memset(ringbuf, 0, sizeof(struct av_ringbuf) * RINGBUF_SIZE);
	ringbuf_rdptr = ringbuf_wrptr = 0;
}

/*
 */
static int av_ringbuf_read(int *type, GtkWidget **w, long *data)
{
	int rp;
	
	if (ringbuf_rdptr == ringbuf_wrptr)
		return 0;
	rp = ringbuf_rdptr;
	*type = ringbuf[rp].type;
	*w = ringbuf[rp].w;
	*data = ringbuf[rp].data;
	rp = (rp + 1) & (RINGBUF_SIZE - 1);
	ringbuf_rdptr = rp;
	return 1;
}

/*
 */
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

/*
 */
static void *midi_loop(void *arg)
{
	midi_status_t *st = (midi_status_t *) arg;
	
	if (rt_prio)
		set_realtime_priority(SCHED_FIFO);
	port_client_do_loop(st->client, 50);
	pthread_exit(NULL);
	return 0;
}

/*
 */
static gboolean idle_cb(gpointer data)
{
	int type;
	GtkWidget *w;
	long val;
	
	while (av_ringbuf_read(&type, &w, &val)) {
		switch (type) {
		case UPDATE_MUTE:
			av_mute_update(w, val, 0);
			break;
		case VEL_COLOR:
			set_vel_bar_color(w, val, 0);
			break;
		case UPDATE_STATUS:
			av_channel_update(w, val, 0);
			break;
		case NOTE_ON:
			av_note_update(w, val, TRUE, 0);
			break;
		case NOTE_OFF:
			av_note_update(w, val, FALSE, 0);
			break;
		case PIANO_RESET:
			av_piano_reset(w, 0);
			break;
		case UPDATE_PGM:
			av_program_update(w, (char *) val, 0);
			break;
		case UPDATE_MODE:
			display_midi_mode(w, 0);
			break;
		case UPDATE_TEMPER_KEYSIG:
			display_temper_keysig(w, 0);
			break;
		case UPDATE_TEMPER_TYPE:
			display_temper_type(w, 0);
			break;
		case HIDE_TT_BUTTON:
			av_hide_tt_button(w, val, 0);
			break;
		}
	}
	usleep(1000);
	return TRUE;
}

/*
 */
static int get_file_desc(midi_status_t *st)
{
#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
	int npfds = snd_seq_poll_descriptors_count(
			port_client_get_seq(st->client), POLLIN);
	struct pollfd pfd;
#endif
	int fd = -1;
	
#if SND_LIB_MAJOR > 0 || SND_LIB_MINOR > 5
	if (npfds == 1 && snd_seq_poll_descriptors(
			port_client_get_seq(st->client), &pfd, npfds, POLLIN) >= 0)
		fd = pfd.fd;
#else
	fd = snd_seq_file_descriptor(port_client_get_seq(st->client));
#endif
	if (fd < 0) {
		fprintf(stderr, "cannot get file descriptor "
				"for ALSA sequencer inputs\n");
		exit(1);
	}
	return fd;
}

/*
 * input handler from Gtk
 */
static void handle_input(gpointer data,
		gint source, GdkInputCondition condition)
{
	midi_status_t *st = (midi_status_t *) data;
	
	port_client_do_event(st->client);
}

/*
 * set realtime prority as maximum
 */
static int set_realtime_priority(int policy)
{
#ifdef HAVE_CAPABILITY_H
	cap_t cp = cap_get_proc();
	cap_value_t caps[] = { CAP_SETCAP, CAP_SYS_NICE };
#endif
	struct sched_param parm;
	
#ifdef HAVE_CAPABILITY_H
	cap_set_flag(cp, flag, 2, &caps, CAP_SET);
	cap_set_proc(cp);
#endif
	memset(&parm, 0, sizeof(parm));
	parm.sched_priority = sched_get_priority_max(policy);
	if (sched_setscheduler(0, policy, &parm)) {
		perror("sched_setscheduler");
		exit(1);
	}
	return 0;
}

