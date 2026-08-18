/*
 * piano.c - piano widget
 *
 * Swami
 * Copyright (C) 1999-2003 Josh Green <jgreen@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA or point your web browser to http://www.gnu.org.
 */
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "piano.h"

/* Forward declarations */

static void piano_class_init (PianoClass * klass);
static void piano_init (Piano * piano);
static void piano_destroy (GObject * object);
#ifdef USE_GTK4
static void piano_snapshot (GtkWidget * widget, GtkSnapshot * snapshot);
static void piano_measure (GtkWidget * widget, GtkOrientation orientation,
			   int for_size, int * minimum, int * natural,
			   int * minimum_baseline, int * natural_baseline);
#else
static void piano_realize (GtkWidget * widget);
static void piano_get_preferred_width (GtkWidget * widget, gint * min, gint * nat);
static void piano_get_preferred_height (GtkWidget * widget, gint * min, gint * nat);
static void piano_size_allocate (GtkWidget * widget, GtkAllocation * allocation);
static gboolean piano_draw (GtkWidget * widget, cairo_t * cr);
#endif
static void draw_keyboard_surface (Piano * piano);
static void draw_key_on_surface (Piano * piano, int note, gboolean pressed);

#define POFSY 0

static struct
{
  guint8 selx;
  guint8 dispx;
  gboolean white;		/* white key or black key? */
}

keyinfo[12] = { {PIANO_KEY_XWID-(PIANO_KEY_XWID/3),     2,                    TRUE},
				{PIANO_KEY_XWID*3/2-(PIANO_KEY_XWID/5), PIANO_KEY_XWID-1,   FALSE},
				{PIANO_KEY_XWID*2-(PIANO_KEY_XWID/3),   PIANO_KEY_XWID+2,     TRUE},
				{PIANO_KEY_XWID*5/2-(PIANO_KEY_XWID/5), PIANO_KEY_XWID*2-1, FALSE},
				{PIANO_KEY_XWID*3-1,                    PIANO_KEY_XWID*2+2,   TRUE},
				{PIANO_KEY_XWID*4-(PIANO_KEY_XWID/3),   PIANO_KEY_XWID*3+2,   TRUE},
				{PIANO_KEY_XWID*9/2-(PIANO_KEY_XWID/5), PIANO_KEY_XWID*4-1, FALSE},
				{PIANO_KEY_XWID*5-(PIANO_KEY_XWID/3),   PIANO_KEY_XWID*4+2,   TRUE},
				{PIANO_KEY_XWID*11/2-(PIANO_KEY_XWID/5),PIANO_KEY_XWID*5-1, FALSE},
				{PIANO_KEY_XWID*6-(PIANO_KEY_XWID/3),   PIANO_KEY_XWID*5+2,   TRUE},
				{PIANO_KEY_XWID*13/2-(PIANO_KEY_XWID/5),PIANO_KEY_XWID*6-1,FALSE},
				{PIANO_KEY_XWID*7-1,                    PIANO_KEY_XWID*6+2,   TRUE}
};

enum
{
  NOTE_ON,
  NOTE_OFF,
  LAST_SIGNAL
};

static guint piano_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(Piano, piano, GTK_TYPE_WIDGET)

static void
piano_class_init (PianoClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = piano_destroy;

#ifdef USE_GTK4
  widget_class->snapshot = piano_snapshot;
  widget_class->measure = piano_measure;
#else
  widget_class->realize = piano_realize;
  widget_class->draw = piano_draw;
  widget_class->get_preferred_width = piano_get_preferred_width;
  widget_class->get_preferred_height = piano_get_preferred_height;
  widget_class->size_allocate = piano_size_allocate;
#endif

  klass->note_on = NULL;
  klass->note_off = NULL;

  piano_signals[NOTE_ON] = g_signal_new ("note-on",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_FIRST,
    G_STRUCT_OFFSET (PianoClass, note_on),
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT,
    G_TYPE_NONE, 1, G_TYPE_UINT);

  piano_signals[NOTE_OFF] = g_signal_new ("note-off",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_FIRST,
    G_STRUCT_OFFSET (PianoClass, note_off),
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT,
    G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
piano_init (Piano * piano)
{
#ifndef USE_GTK4
  gtk_widget_set_has_window (GTK_WIDGET (piano), TRUE);
#endif
}

GtkWidget *
piano_new (gboolean * selkeys)
{
  Piano *piano;

  piano = g_object_new (piano_get_type (), NULL);

  if (selkeys == NULL)
    selkeys = g_malloc0 (sizeof (gboolean) * 128);
  piano->selkeys = selkeys;

  return GTK_WIDGET (piano);
}

/* draws specified key in its "on" state */
void
piano_note_on (Piano * piano, guint8 keynum)
{
  gint xval, mod;

  g_return_if_fail (piano != NULL);
  g_return_if_fail (IS_PIANO (piano));
  g_return_if_fail (keynum < 128);

  if (piano->selkeys[keynum])
    return;			/* already selected */

  g_signal_emit (G_OBJECT (piano), piano_signals[NOTE_ON], 0, keynum);

  piano->selkeys[keynum] = TRUE;

  if (!piano->keyb_surface)
    return;

  draw_key_on_surface (piano, keynum, TRUE);

  mod = keynum % 12;
  xval = keynum / 12 * (PIANO_KEY_XWID * 7) + keyinfo[mod].dispx;
#ifdef USE_GTK4
  gtk_widget_queue_draw (GTK_WIDGET (piano));
#else
  if (keyinfo[mod].white)
    gtk_widget_queue_draw_area (GTK_WIDGET (piano),
      xval - 1, PIANO_DEFAULT_SIZEY - 8 + POFSY,
      PIANO_KEY_XWID - 1, 8);
  else
    gtk_widget_queue_draw_area (GTK_WIDGET (piano),
      xval, PIANO_DEFAULT_SIZEY / 5 + POFSY,
      PIANO_KEY_XWID / 2 + 1, 8);
#endif
}

/* draws specified key in its "released" state */
void
piano_note_off (Piano * piano, guint8 keynum)
{
  gint xval, mod;

  g_return_if_fail (piano != NULL);
  g_return_if_fail (IS_PIANO (piano));
  g_return_if_fail (keynum < 128);

  if (!piano->selkeys[keynum])
    return;			/* already unselected */

  g_signal_emit (G_OBJECT (piano), piano_signals[NOTE_OFF], 0, keynum);

  piano->selkeys[keynum] = FALSE;

  if (!piano->keyb_surface)
    return;

  draw_key_on_surface (piano, keynum, FALSE);

  mod = keynum % 12;
  xval = keynum / 12 * 7 * PIANO_KEY_XWID + keyinfo[mod].dispx;
#ifdef USE_GTK4
  gtk_widget_queue_draw (GTK_WIDGET (piano));
#else
  if (keyinfo[mod].white)
    gtk_widget_queue_draw_area (GTK_WIDGET (piano),
      xval - 1, PIANO_DEFAULT_SIZEY - 8 + POFSY,
      PIANO_KEY_XWID - 1, 8);
  else
    gtk_widget_queue_draw_area (GTK_WIDGET (piano),
      xval, PIANO_DEFAULT_SIZEY / 5 + POFSY,
      PIANO_KEY_XWID / 2 + 1, 8);
#endif
}

/* converts a key number to x position in pixels to center of key */
gint
piano_key_to_xpos (guint8 keynum)
{
  gint mod, xval;

  if (keynum > 127)
    keynum = 127;

  mod = keynum % 12;
  xval = keynum / 12 * (PIANO_KEY_XWID * 7);

  if (keyinfo[mod].white)
    xval += 2;
  else
    xval += 2;

  /* slight adjustments for adjacent white keys, looks like center */
  if (mod == 4 || mod == 11)
    xval++;
  else if (mod == 5 || mod == 0)
    xval--;

  return (xval);
}

/* converts a pixel x position to key number */
guint8
piano_xpos_to_key (gint xpos)
{
  gint xval, i;
  guint8 keynum;

  xval = xpos % (PIANO_KEY_XWID * 7);	/* pixel offset into keyboard octave */
  for (i = 0; i < 12; i++)		/* loop through key selection offsets */
    if (xval <= keyinfo[i].selx)
      break;			/* is offset within key select */
  keynum = xpos / (PIANO_KEY_XWID * 7) * 12 + i;	/* calc key number */

  if (keynum > 127)
    keynum = 127;

  return (keynum);
}

static void
piano_destroy (GObject * object)
{
  Piano *piano;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_PIANO (object));

  piano = PIANO (object);

  if (piano->keyb_surface)
    {
      cairo_surface_destroy (piano->keyb_surface);
      piano->keyb_surface = NULL;
    }
  g_free (piano->selkeys);
  piano->selkeys = NULL;

  G_OBJECT_CLASS (piano_parent_class)->finalize (object);
}

#ifdef USE_GTK4

static void
piano_snapshot (GtkWidget * widget, GtkSnapshot * snapshot)
{
  Piano *piano;
  graphene_rect_t rect;
  cairo_t *cr;
  int width, height;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_PIANO (widget));

  piano = PIANO (widget);
  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (!piano->keyb_surface)
    {
      piano->keyb_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
        PIANO_DEFAULT_SIZEX, PIANO_DEFAULT_SIZEY);
      draw_keyboard_surface (piano);
    }

  if (piano->keyb_surface)
    {
      rect = GRAPHENE_RECT_INIT (0, 0, width, height);
      cr = gtk_snapshot_append_cairo (snapshot, &rect);
      cairo_set_source_surface (cr, piano->keyb_surface, 0, 0);
      cairo_paint (cr);
      cairo_destroy (cr);
    }
}

static void
piano_measure (GtkWidget * widget, GtkOrientation orientation, int for_size,
	       int * minimum, int * natural,
	       int * minimum_baseline, int * natural_baseline)
{
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    *minimum = *natural = PIANO_DEFAULT_SIZEX + 5;
  else
    *minimum = *natural = PIANO_DEFAULT_SIZEY;
  if (minimum_baseline)
    *minimum_baseline = -1;
  if (natural_baseline)
    *natural_baseline = -1;
}

#else /* GTK3 */

static void
piano_realize (GtkWidget * widget)
{
  Piano *piano;
  GdkWindowAttr attributes;
  GtkAllocation allocation;
  GdkWindow *window;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_PIANO (widget));

  piano = PIANO (widget);
  gtk_widget_set_realized (widget, TRUE);
  gtk_widget_get_allocation (widget, &allocation);

  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.event_mask = gtk_widget_get_events (widget) |
    GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
    GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
    GDK_POINTER_MOTION_HINT_MASK;
  attributes.visual = gtk_widget_get_visual (widget);

  window = gdk_window_new (gtk_widget_get_parent_window (widget),
    &attributes, GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
  gtk_widget_set_window (widget, window);
  gdk_window_set_user_data (window, widget);

  if (piano->keyb_surface)
    cairo_surface_destroy (piano->keyb_surface);
  piano->keyb_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
    PIANO_DEFAULT_SIZEX, PIANO_DEFAULT_SIZEY);
  draw_keyboard_surface (piano);
}

static void
piano_get_preferred_width (GtkWidget * widget, gint * min, gint * nat)
{
  *min = *nat = PIANO_DEFAULT_SIZEX + 5;
}

static void
piano_get_preferred_height (GtkWidget * widget, gint * min, gint * nat)
{
  *min = *nat = PIANO_DEFAULT_SIZEY;
}

static void
piano_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_PIANO (widget));
  g_return_if_fail (allocation != NULL);

  gtk_widget_set_allocation (widget, allocation);

  if (gtk_widget_get_realized (widget))
    gdk_window_move_resize (gtk_widget_get_window (widget),
      allocation->x, allocation->y,
      allocation->width, allocation->height);
}

/* Fast blit of the pre-rendered backing surface */
static gboolean
piano_draw (GtkWidget * widget, cairo_t * cr)
{
  Piano *piano;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (IS_PIANO (widget), FALSE);

  piano = PIANO (widget);

  if (piano->keyb_surface)
    {
      cairo_set_source_surface (cr, piano->keyb_surface, 0, 0);
      cairo_paint (cr);
    }

  return FALSE;
}

#endif /* USE_GTK4 */

/* Draw (or redraw) a single key into the backing surface */
static void
draw_key_on_surface (Piano * piano, int note, gboolean pressed)
{
  cairo_t *cr;
  int mod, xval;

  if (!piano->keyb_surface)
    return;

  mod = note % 12;
  xval = (note / 12) * (PIANO_KEY_XWID * 7) + keyinfo[mod].dispx;

  cr = cairo_create (piano->keyb_surface);

  if (keyinfo[mod].white)
    {
      int x = xval - 1;
      int y = PIANO_DEFAULT_SIZEY - 8 + POFSY;
      int w = PIANO_KEY_XWID - 1;	/* = 4 */

      /* White key bottom region: restore to white */
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_rectangle (cr, x, y, w, 8);
      cairo_fill (cr);

      if (pressed)
        {
          /* Pink highlight strip in upper half */
          cairo_set_source_rgb (cr, 1.0, 0.0, 0.5);
          cairo_rectangle (cr, x + PIANO_KEY_XWID / 4, y, PIANO_KEY_XWID / 2, 4);
          cairo_fill (cr);
          /* White lower half — shadow lines intentionally absent when pressed */
          cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
          cairo_rectangle (cr, x, y + 4, w, 4);
          cairo_fill (cr);
        }
      else
        {
          /* Restore the horizontal lines erased by the white fill */
          cairo_set_line_width (cr, 1.0);
          cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
          cairo_move_to (cr, x, PIANO_DEFAULT_SIZEY - 3.5 + POFSY);
          cairo_line_to (cr, x + w, PIANO_DEFAULT_SIZEY - 3.5 + POFSY);
          cairo_stroke (cr);
          cairo_set_source_rgb (cr, 0.75, 0.75, 0.75);
          cairo_move_to (cr, x, PIANO_DEFAULT_SIZEY - 2.5 + POFSY);
          cairo_line_to (cr, x + w, PIANO_DEFAULT_SIZEY - 2.5 + POFSY);
          cairo_stroke (cr);
          cairo_move_to (cr, x, PIANO_DEFAULT_SIZEY - 1.5 + POFSY);
          cairo_line_to (cr, x + w, PIANO_DEFAULT_SIZEY - 1.5 + POFSY);
          cairo_stroke (cr);
          cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
          cairo_move_to (cr, x, PIANO_DEFAULT_SIZEY - 0.5 + POFSY);
          cairo_line_to (cr, x + w, PIANO_DEFAULT_SIZEY - 0.5 + POFSY);
          cairo_stroke (cr);
        }
    }
  else
    {
      int x = xval;
      int y = PIANO_DEFAULT_SIZEY / 5 + POFSY;	/* = 4 */
      int w = PIANO_KEY_XWID / 2 + 1;	/* = 3 */

      if (pressed)
        {
          /* Magenta highlight top 5 rows */
          cairo_set_source_rgb (cr, 1.0, 0.0, 0.5);
          cairo_rectangle (cr, x, y, w, 5);
          cairo_fill (cr);
          /* Black bottom rows */
          cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
          cairo_rectangle (cr, x, y + 5, w, 2);
          cairo_fill (cr);
          /* Black bottom line */
          cairo_move_to (cr, x, y + 7);
          cairo_line_to (cr, x + w, y + 7);
          cairo_stroke (cr);
        }
      else
        {
          /* Solid black */
          cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
          cairo_rectangle (cr, x, y, w, 8);
          cairo_fill (cr);
        }
    }

  cairo_destroy (cr);
}

/* Draw the full keyboard into the backing surface (all keys unpressed) */
static void
draw_keyboard_surface (Piano * piano)
{
  cairo_t *cr;
  int i, x, mod;

  if (!piano->keyb_surface)
    return;

  cr = cairo_create (piano->keyb_surface);

  /* White background */
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_rectangle (cr, 1, 1 + POFSY, PIANO_DEFAULT_SIZEX - 2, PIANO_DEFAULT_SIZEY - 2);
  cairo_fill (cr);

  /* Top and bottom border lines */
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_line_width (cr, 1.0);

  cairo_move_to (cr, 0, 0.5 + POFSY);
  cairo_line_to (cr, PIANO_DEFAULT_SIZEX, 0.5 + POFSY);
  cairo_stroke (cr);

  cairo_move_to (cr, 0, PIANO_DEFAULT_SIZEY - 0.5 + POFSY);
  cairo_line_to (cr, PIANO_DEFAULT_SIZEX, PIANO_DEFAULT_SIZEY - 0.5 + POFSY);
  cairo_stroke (cr);

  cairo_move_to (cr, 1, PIANO_DEFAULT_SIZEY - 3.5 + POFSY);
  cairo_line_to (cr, PIANO_DEFAULT_SIZEX - 1, PIANO_DEFAULT_SIZEY - 3.5 + POFSY);
  cairo_stroke (cr);

  /* Shadow lines (gray) — original shadclr = 49152/65535 ≈ 0.75 */
  cairo_set_source_rgb (cr, 0.75, 0.75, 0.75);
  cairo_move_to (cr, 1, PIANO_DEFAULT_SIZEY - 2.5 + POFSY);
  cairo_line_to (cr, PIANO_DEFAULT_SIZEX - 1, PIANO_DEFAULT_SIZEY - 2.5 + POFSY);
  cairo_stroke (cr);
  cairo_move_to (cr, 1, PIANO_DEFAULT_SIZEY - 1.5 + POFSY);
  cairo_line_to (cr, PIANO_DEFAULT_SIZEX - 1, PIANO_DEFAULT_SIZEY - 1.5 + POFSY);
  cairo_stroke (cr);

  /* White key separators and black key rectangles */
  for (i = 0, x = 0; i < 76; i++, x += PIANO_KEY_XWID)
    {
      /* White key separator line */
      cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
      cairo_move_to (cr, x + 0.5, 1 + POFSY);
      cairo_line_to (cr, x + 0.5, PIANO_DEFAULT_SIZEY - 1 + POFSY);
      cairo_stroke (cr);

      /* Black key rectangle (not on positions 0, 3 or last key) */
      mod = i % 7;
      if ((mod != 0) && (mod != 3) && (i != 75))
        {
          cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
          cairo_rectangle (cr, x - 1, 1 + POFSY,
            PIANO_KEY_XWID / 2 + 1, PIANO_DEFAULT_SIZEY / 2);
          cairo_fill (cr);
        }
    }

  /* Middle-C marker (C60) — original c60clr = 18000/65535 ≈ 0.27, 0, 54000/65535 ≈ 0.82 */
  cairo_set_source_rgb (cr, 0.27, 0.0, 0.82);
  cairo_rectangle (cr,
    PIANO_DEFAULT_SIZEX / 2 - PIANO_DEFAULT_SIZEX / 33 - 1,
    PIANO_DEFAULT_SIZEY - 13,
    PIANO_KEY_XWID * 2 / 3, 4);
  cairo_fill (cr);

  cairo_destroy (cr);
}
