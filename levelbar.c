/*
 * statusbar.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "levelbar.h"
#include <stdlib.h>

/*
 * status bar instance record
 */
enum {
	BAR_TYPE_LEVEL,
	BAR_TYPE_SOLID,
	BAR_TYPE_ARROW,
};

typedef struct status_bar_t status_bar_t;
struct status_bar_t {
	unsigned short type;	/* enum type */
	unsigned short width, height;	/* widget size */
	int minval, maxval, defval, curval, cached_val;	/* values */
	unsigned short drawn, step;
	GdkColor color;
	char delayed, updated;
	guint32 timer;
};


/*
 * level bar instance record
 */
typedef struct level_bar_t level_bar_t;
struct level_bar_t {
	status_bar_t st;	/* inherited */
	int level_count;
	int level;
	unsigned short lv_drawn;
	int fall_dec;
	GdkColor lv_color;
};

/* constants for level bar */
#define LEVEL_STEP		3	/* step */
#define BAR_TIMER_PERIOD	20	/* msec */
#define FALLING_COUNT		40	/* larger is longer */
#define FALLING_SCALE		10	/* larger is slower */

/*
 * protoypes
 */
static GtkWidget *bar_widget_new(status_bar_t *bar, int type, int width, int height, int minval, int maxval, int defval, int delayed, int step);
static gint update_timer(GtkWidget *w);
static void fall_level(GtkWidget *w, level_bar_t *lv);
static void draw_level(GtkWidget *w, level_bar_t *arg);
static void draw_solid(GtkWidget *w, status_bar_t *arg);
static void draw_arrow(GtkWidget *w, status_bar_t *arg);
static void update_bar(GtkWidget *w, status_bar_t *arg, int curval);
static int expose_bar(GtkWidget *w);


/*
 * local common variables
 */
static GdkGC *gc;	/* we use the shared gc */

/*
 * align to the step size (for level bar)
 */
static inline int
align_step(status_bar_t *bar, int val)
{
	if (bar->step > 1)
		return (val / bar->step) * bar->step;
	return val;
}

/*
 * convert to pixel
 */
static inline int
convert_drawn(status_bar_t *bar, int val)
{
	val = (val - bar->minval) * (bar->width - 1);
	val /= (bar->maxval - bar->minval);
	return align_step(bar, val);
}

/*
 * create a level bar type
 */
GtkWidget *
level_bar_new(int width, int height, int minval, int maxval, int defval)
{
	level_bar_t *lv;
	GtkWidget *w;

	lv = g_malloc0(sizeof(*lv));
	w = bar_widget_new(&lv->st, BAR_TYPE_LEVEL, width, height,
			   minval, maxval, defval, TRUE, LEVEL_STEP);
	lv->level_count = 0;
	lv->fall_dec = (width + FALLING_SCALE - 1) / FALLING_SCALE;
	lv->level = lv->lv_drawn = lv->st.drawn;
	alloc_color(&lv->lv_color, 0xffff, 0xffff, 0xffff);

	return w;
}

/*
 * create a solid bar type
 */
GtkWidget *
solid_bar_new(int width, int height, int minval, int maxval, int defval, int delayed)
{
	return bar_widget_new(NULL, BAR_TYPE_SOLID, width, height,
			      minval, maxval, defval, delayed, 1);
}

/*
 * create an arrow bar type
 */
GtkWidget *
arrow_bar_new(int width, int height, int minval, int maxval, int defval, int delayed)
{
	return bar_widget_new(NULL, BAR_TYPE_ARROW, width, height,
			      minval, maxval, defval, delayed, 1);
}

/*
 * update value
 */
void
channel_status_bar_update(GtkWidget *w, int val)
{
	status_bar_t *bar = gtk_object_get_user_data(GTK_OBJECT(w));
	if (bar->curval != val)
		update_bar(w, bar, val);
}

/*
 * set body color
 */
void
channel_status_bar_set_color_rgb(GtkWidget *w, int r, int g, int b)
{
	status_bar_t *bar = gtk_object_get_user_data(GTK_OBJECT(w));
	alloc_color(&bar->color, r, g, b);
}

/*
 * set color for level bar
 */
void
level_bar_set_level_color_rgb(GtkWidget *w, int r, int g, int b)
{
	level_bar_t *bar = gtk_object_get_user_data(GTK_OBJECT(w));
	alloc_color(&bar->lv_color, r, g, b);
}

/*
 * skeleton to create widget and to initialize instance
 */
static GtkWidget *
bar_widget_new(status_bar_t *bar, int type, int width, int height,
	       int minval, int maxval, int defval, int delayed, int step)
{
	GtkWidget *w;

	if (bar == NULL)
		bar = g_malloc0(sizeof(*bar));
	bar->type = type;
	bar->width = width;
	bar->height = height;
	bar->minval = minval;
	bar->maxval = maxval;
	bar->defval = defval;
	bar->curval = defval;
	bar->cached_val = defval;
	bar->delayed = delayed;
	bar->step = step;
	bar->drawn = convert_drawn(bar, defval);

	alloc_color(&bar->color, 0xffff, 0xffff, 0xffff);

	w = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(w), width, height);
	gtk_object_set_user_data(GTK_OBJECT(w), bar);
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
			   GTK_SIGNAL_FUNC(expose_bar),
			   NULL);
	if (delayed)
		bar->timer = gtk_timeout_add(BAR_TIMER_PERIOD,
					     (GtkFunction)update_timer,
					     (gpointer)w);

	return w;
}

/*
 * allocate color
 */
void
alloc_color(GdkColor *color, int red, int green, int blue)
{
	GdkColormap *cmap;

	cmap = gdk_colormap_get_system();
	color->red = red;
	color->green = green;
	color->blue = blue;
	if (! gdk_color_alloc(cmap, color))
		g_error("can't allocate color");
}


/*
 * timer callback
 */
static gint
update_timer(GtkWidget *w)
{
	status_bar_t *bar = gtk_object_get_user_data(GTK_OBJECT(w));
	int drawn;
	if (bar->updated) {
		drawn = convert_drawn(bar, bar->cached_val);
		if (drawn == bar->drawn)
			bar->updated = FALSE;
		else
			bar->drawn = drawn;
	}
	if (bar->type == BAR_TYPE_LEVEL)
		fall_level(w, (level_bar_t*)bar);
	if (bar->updated)
		expose_bar(w);
	if (bar->cached_val != bar->curval) {
		bar->cached_val = bar->curval;
		bar->updated = TRUE;
	} else
		bar->updated = FALSE;
	return TRUE;
}

/*
 * update level bar
 */
static void
fall_level(GtkWidget *w, level_bar_t *lv)
{
	int drawn;

	if (lv->level < lv->st.drawn) {
		lv->level = lv->lv_drawn = lv->st.drawn;
		lv->level_count = FALLING_COUNT;
		lv->st.updated = TRUE;
	} else if (lv->level_count > 0) {
		lv->level_count--;
	} else if (lv->level > 0) {
		lv->level -= lv->fall_dec;
		if (lv->level < 0)
			lv->level = 0;
		if (lv->level < lv->st.drawn) {
			lv->level = lv->st.drawn;
			lv->level_count = FALLING_COUNT;
		}
		drawn = align_step(&lv->st, lv->level);
		if (drawn != lv->lv_drawn) {
			lv->lv_drawn = drawn;
			lv->st.updated = TRUE;
		}
	}
}

/*
 * draw level bar
 */
static void
draw_level(GtkWidget *w, level_bar_t *arg)
{
	int i;

	gdk_draw_rectangle(w->window,
			   w->style->black_gc,
			   TRUE,
			   0, 0, arg->st.width, arg->st.height);
	gdk_gc_set_foreground(gc, &arg->st.color);
	for (i = 0; i < arg->st.drawn; i += LEVEL_STEP)
		gdk_draw_rectangle(w->window, gc, TRUE,
				   i, 0, LEVEL_STEP-1, arg->st.height);
	gdk_gc_set_foreground(gc, &arg->lv_color);
	gdk_draw_rectangle(w->window, gc, TRUE, arg->lv_drawn, 0,
			   LEVEL_STEP-1, arg->st.height);
}

/*
 * draw solid bar
 */
static void
draw_solid(GtkWidget *w, status_bar_t *arg)
{
	gdk_draw_rectangle(w->window,
			   w->style->black_gc,
			   TRUE,
			   0, 0, arg->width, arg->height);
	gdk_gc_set_foreground(gc, &arg->color);
	gdk_draw_rectangle(w->window,
			   gc, TRUE,
			   0, 0, arg->drawn + 1, arg->height);
}

/*
 * draw arrow bar
 */
static void
draw_arrow(GtkWidget *w, status_bar_t *arg)
{
	GdkPoint p[3];

	gdk_draw_rectangle(w->window,
			   w->style->black_gc,
			   TRUE,
			   0, 0, arg->width, arg->height);
	p[0].x = arg->drawn;
	p[0].y = arg->height / 2;
	p[1].x = arg->width - arg->drawn - 1;
	p[1].y = 0;
	p[2].x = p[1].x;
	p[2].y = arg->height - 1;
	gdk_gc_set_foreground(gc, &arg->color);
	gdk_draw_polygon(w->window,
			 gc, TRUE, p, 3);
	gdk_draw_line(w->window,
		      gc, p[1].x, p[1].y, p[2].x, p[2].y);
}

/*
 * calculate the absolute distance
 */
static inline int
val_diff(status_bar_t *arg, int val)
{
	val -= arg->defval;
	return abs(val);
}

/*
 * update current value
 */
static void
update_bar(GtkWidget *w, status_bar_t *arg, int curval)
{
	int drawn;

	arg->curval = curval;
	if (arg->delayed) {
		/* redrawn in timeout callback -
		 * we here only check the highet value
		 */
		int delta = val_diff(arg, curval);
		int delta_c = val_diff(arg, arg->cached_val);
		if (delta < delta_c)
			return;
		arg->cached_val = curval; /* remember the highest value */
		arg->updated = TRUE;
	} else {
		/* redraw now if necessary */
		drawn = convert_drawn(arg, curval);
		if (drawn == arg->drawn)
			return;
		arg->drawn = drawn;
		expose_bar(w);
	}
}

/*
 * signal exposed_event
 */
static int
expose_bar(GtkWidget *w)
{
	status_bar_t *arg = gtk_object_get_user_data(GTK_OBJECT(w));
	if (gc == NULL)
		gc = gdk_gc_new(w->window); /* FIXME: i know it's not good.. */
	switch (arg->type) {
	case BAR_TYPE_LEVEL:
		draw_level(w, (level_bar_t*)arg);
		break;
	case BAR_TYPE_SOLID:
		draw_solid(w, arg);
		break;
	case BAR_TYPE_ARROW:
		draw_arrow(w, arg);
		break;
	}
	return FALSE;
}
