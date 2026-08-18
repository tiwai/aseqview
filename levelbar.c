/*
 * statusbar.c
 *
 * Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
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

#ifdef USE_GTK4
/* GTK4: draw function registered via gtk_drawing_area_set_draw_func */
static void draw_bar (GtkDrawingArea * da, cairo_t * cr,
		      int width, int height, gpointer data);
#else
static gboolean draw_bar (GtkWidget * w, cairo_t * cr, gpointer data);
#endif

/*
 * RGB color stored as doubles in [0.0, 1.0]
 */
typedef struct {
	double r, g, b;
} BarColor;

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
	BarColor color;
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
	BarColor lv_color;
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
static gboolean update_timer(gpointer data);
static void fall_level(GtkWidget *w, level_bar_t *lv);
static void draw_level(cairo_t *cr, level_bar_t *arg);
static void draw_solid(cairo_t *cr, status_bar_t *arg);
static void draw_arrow(cairo_t *cr, status_bar_t *arg);
static void update_bar(GtkWidget *w, status_bar_t *arg, int curval);

/*
 * store color as doubles
 */
static void
alloc_color(BarColor *color, int red, int green, int blue)
{
	color->r = red / 65535.0;
	color->g = green / 65535.0;
	color->b = blue / 65535.0;
}

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
	status_bar_t *bar = g_object_get_data(G_OBJECT(w), "bar_data");
	if (bar->curval != val)
		update_bar(w, bar, val);
}

/*
 * set body color
 */
void
channel_status_bar_set_color_rgb(GtkWidget *w, int r, int g, int b)
{
	status_bar_t *bar = g_object_get_data(G_OBJECT(w), "bar_data");
	alloc_color(&bar->color, r, g, b);
}

/*
 * set color for level bar
 */
void
level_bar_set_level_color_rgb(GtkWidget *w, int r, int g, int b)
{
	level_bar_t *bar = g_object_get_data(G_OBJECT(w), "bar_data");
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
	gtk_widget_set_size_request(w, width, height);
	g_object_set_data(G_OBJECT(w), "bar_data", bar);
#ifdef USE_GTK4
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(w), draw_bar, NULL, NULL);
#else
	gtk_widget_set_events(w, GDK_EXPOSURE_MASK);
	g_signal_connect(G_OBJECT(w), "draw", G_CALLBACK(draw_bar), NULL);
#endif
	if (delayed)
		bar->timer = g_timeout_add(BAR_TIMER_PERIOD, update_timer, w);

	return w;
}

/*
 * timer callback
 */
static gboolean
update_timer(gpointer data)
{
	GtkWidget *w = GTK_WIDGET(data);
	status_bar_t *bar = g_object_get_data(G_OBJECT(w), "bar_data");
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
		gtk_widget_queue_draw(w);
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
draw_level(cairo_t *cr, level_bar_t *arg)
{
	int i;

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, arg->st.width, arg->st.height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, arg->st.color.r, arg->st.color.g, arg->st.color.b);
	for (i = 0; i < arg->st.drawn; i += LEVEL_STEP) {
		cairo_rectangle(cr, i, 0, LEVEL_STEP - 1, arg->st.height);
		cairo_fill(cr);
	}

	cairo_set_source_rgb(cr, arg->lv_color.r, arg->lv_color.g, arg->lv_color.b);
	cairo_rectangle(cr, arg->lv_drawn, 0, LEVEL_STEP - 1, arg->st.height);
	cairo_fill(cr);
}

/*
 * draw solid bar
 */
static void
draw_solid(cairo_t *cr, status_bar_t *arg)
{
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, arg->width, arg->height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, arg->color.r, arg->color.g, arg->color.b);
	cairo_rectangle(cr, 0, 0, arg->drawn + 1, arg->height);
	cairo_fill(cr);
}

/*
 * draw arrow bar
 */
static void
draw_arrow(cairo_t *cr, status_bar_t *arg)
{
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, arg->width, arg->height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, arg->color.r, arg->color.g, arg->color.b);
	cairo_move_to(cr, arg->drawn + 1, arg->height / 2);
	cairo_line_to(cr, arg->width - arg->drawn - 1, 0);
	cairo_line_to(cr, arg->width - arg->drawn - 1, arg->height - 1);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, arg->color.r, arg->color.g, arg->color.b);
	cairo_move_to(cr, arg->width - arg->drawn - 1, 0);
	cairo_line_to(cr, arg->width - arg->drawn - 1, arg->height - 1);
	cairo_stroke(cr);
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
		 * we here only check the highest value
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
		gtk_widget_queue_draw(w);
	}
}

/*
 * draw callback — implementation shared between GTK3 and GTK4
 */
static void
draw_bar_impl(GObject *obj, cairo_t *cr)
{
	status_bar_t *arg = g_object_get_data(obj, "bar_data");

	switch (arg->type) {
	case BAR_TYPE_LEVEL:
		draw_level(cr, (level_bar_t*)arg);
		break;
	case BAR_TYPE_SOLID:
		draw_solid(cr, arg);
		break;
	case BAR_TYPE_ARROW:
		draw_arrow(cr, arg);
		break;
	}
}

#ifdef USE_GTK4
static void
draw_bar(GtkDrawingArea *da, cairo_t *cr, int width, int height, gpointer data)
{
	draw_bar_impl(G_OBJECT(da), cr);
}
#else
static gboolean
draw_bar(GtkWidget *w, cairo_t *cr, gpointer data)
{
	draw_bar_impl(G_OBJECT(w), cr);
	return FALSE;
}
#endif
