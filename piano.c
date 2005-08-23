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

#include "piano.h"

/* Forward declarations */

static void piano_class_init (PianoClass * klass);
static void piano_init (Piano * piano);
static void piano_destroy (GtkObject * object);
static void piano_realize (GtkWidget * widget);
static void piano_size_request (GtkWidget * widget,
  GtkRequisition * requisition);
static void piano_size_allocate (GtkWidget * widget,
  GtkAllocation * allocation);
static gint piano_expose (GtkWidget * widget, GdkEventExpose * event);
static gint piano_button_press (GtkWidget * widget, GdkEventButton * event);
static gint piano_button_release (GtkWidget * widget, GdkEventButton * event);
static gint piano_motion_notify (GtkWidget * widget, GdkEventMotion * event);
static void piano_update_mouse (Piano * piano, gint x, gint y);

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

/* color used for note C60 marker */
static GdkColor c60clr = { red : 18000, green : 0, blue : 54000 };

static GtkWidgetClass *parent_class = NULL;

guint
piano_get_type (void)
{
  static guint piano_type = 0;

  if (!piano_type)
    {
      GtkTypeInfo piano_info = {
	"Piano",
	sizeof (Piano),
	sizeof (PianoClass),
	(GtkClassInitFunc) piano_class_init,
	(GtkObjectInitFunc) piano_init,
	NULL,
	NULL,
      };
      piano_type = gtk_type_unique (gtk_widget_get_type (), &piano_info);
    }

  return piano_type;
}

enum
{
  NOTE_ON,
  NOTE_OFF,
  DUMMY_SIGNAL			/* used to count signals */
};

static guint piano_signals[DUMMY_SIGNAL] = { 0 };

#ifndef GTK_CLASS_TYPE
#define GTK_CLASS_TYPE(x)	((x)->type)
#endif

static void
piano_class_init (PianoClass * klass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;

  parent_class = gtk_type_class (gtk_widget_get_type ());

  piano_signals[NOTE_ON] = gtk_signal_new ("note-on",
    GTK_RUN_FIRST, GTK_CLASS_TYPE(object_class),
    GTK_SIGNAL_OFFSET (PianoClass, note_on),
    gtk_marshal_NONE__UINT, GTK_TYPE_NONE, 1, GTK_TYPE_UINT);

  piano_signals[NOTE_OFF] = gtk_signal_new ("note-off",
    GTK_RUN_FIRST, GTK_CLASS_TYPE(object_class),
    GTK_SIGNAL_OFFSET (PianoClass, note_off),
    gtk_marshal_NONE__UINT, GTK_TYPE_NONE, 1, GTK_TYPE_UINT);

#if GTK_MAJOR_VERSION < 2
  gtk_object_class_add_signals (object_class, piano_signals, DUMMY_SIGNAL);
#endif
  klass->note_on = NULL;
  klass->note_off = NULL;

  object_class->destroy = piano_destroy;

  widget_class->realize = piano_realize;
  widget_class->expose_event = piano_expose;
  widget_class->size_request = piano_size_request;
  widget_class->size_allocate = piano_size_allocate;
  widget_class->button_press_event = NULL; //piano_button_press;
  widget_class->button_release_event = NULL; //piano_button_release;
  widget_class->motion_notify_event = NULL; //piano_motion_notify;
}

/* -----------------------------------------------
 initialize piano widget
 Draws piano pixmap and black/white (un)selected state pixmaps
 Sets variables to default states
----------------------------------------------- */
static void
piano_init (Piano * piano)
{
}

GtkWidget *
piano_new (gboolean * selkeys)
{
  Piano *piano;

  piano = gtk_type_new (piano_get_type ());

  if (selkeys==NULL) {
	  selkeys = (gboolean*)malloc(sizeof(gboolean)*128);
  }
  piano->selkeys = selkeys;

  return GTK_WIDGET (piano);
}

/* draws specified key in its "on" state */
void
piano_note_on (Piano * piano, guint8 keynum)
{
  gint xval, mod;
  GdkRectangle uparea;		/* update area rectangle */

  if (!piano)
	  return;
  g_return_if_fail (piano != NULL);
  g_return_if_fail (IS_PIANO (piano));
  g_return_if_fail (keynum < 128);

  if (piano->selkeys[keynum])
    return;			/* already selected? */

  /* run user piano key press handler */
  gtk_signal_emit (GTK_OBJECT (piano), piano_signals[NOTE_ON], keynum);

  piano->selkeys[keynum] = TRUE;

  mod = keynum % 12;
  xval = keynum / 12 * (PIANO_KEY_XWID*7) + keyinfo[mod].dispx;
  if (keyinfo[mod].white)
    {
      gdk_draw_pixmap (piano->keyb_pm,
	piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
	piano->selw_pm, 0, 0, xval - 1, PIANO_DEFAULT_SIZEY-8 + POFSY, PIANO_KEY_XWID-1, 8);
      uparea.x = xval - 1;
      uparea.y = PIANO_DEFAULT_SIZEY-8 + POFSY;
      uparea.width = PIANO_KEY_XWID-1;
    }
  else
    {
      gdk_draw_pixmap (piano->keyb_pm,
	piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
	piano->selb_pm, 0, 0, xval, PIANO_DEFAULT_SIZEY/5 + POFSY, PIANO_KEY_XWID/2+1, 8);
      uparea.x = xval;
      uparea.y = PIANO_DEFAULT_SIZEY/5 + POFSY;
      uparea.width = PIANO_KEY_XWID/2+1;
    }
  uparea.height = 8;
  gtk_widget_draw (&piano->widget, &uparea);
}

/* draws specified key in its "released" state */
void
piano_note_off (Piano * piano, guint8 keynum)
{
  gint xval, mod;
  GdkRectangle uparea;		/* update area */

  g_return_if_fail (piano != NULL);
  g_return_if_fail (IS_PIANO (piano));
  g_return_if_fail (keynum < 128);

  if (!piano->selkeys[keynum])
    return;			/* already unselected? */

  /* signal user piano release key handlers */
  gtk_signal_emit (GTK_OBJECT (piano), piano_signals[NOTE_OFF], keynum);

  piano->selkeys[keynum] = FALSE;

  mod = keynum % 12;
  xval = keynum / 12 * 7 * PIANO_KEY_XWID + keyinfo[mod].dispx;
  if (keyinfo[mod].white)
    {
      gdk_draw_pixmap (piano->keyb_pm,
	piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
	piano->unsw_pm, 0, 0, xval - 1, PIANO_DEFAULT_SIZEY-8 + POFSY, PIANO_KEY_XWID-1, 8);
      uparea.x = xval - 1;
      uparea.y = PIANO_DEFAULT_SIZEY-8 + POFSY;
      uparea.width = PIANO_KEY_XWID-1;
    }
  else
    {
      gdk_draw_pixmap (piano->keyb_pm,
	piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
	piano->unsb_pm, 0, 0, xval, PIANO_DEFAULT_SIZEY/5 + POFSY, PIANO_KEY_XWID/2+1, 8);
      uparea.x = xval;
      uparea.y = PIANO_DEFAULT_SIZEY/5 + POFSY;
      uparea.width = PIANO_KEY_XWID/2+1;
    }
  uparea.height = 8;
  gtk_widget_draw (&piano->widget, &uparea);
}

/* converts a key number to x position in pixels to center of key */
gint
piano_key_to_xpos (guint8 keynum)
{
  gint mod, xval;

  if (keynum > 127)
    keynum = 127;

  mod = keynum % 12;
  xval = keynum / 12 * (PIANO_KEY_XWID*7);// + keyinfo[mod].dispx;

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

  xval = xpos % (PIANO_KEY_XWID*7);		/* pixel offset into keyboard octave */
  for (i = 0; i < 12; i++)	/* loop through key selection offsets */
    if (xval <= keyinfo[i].selx)
      break;			/* is offset within key select */
  keynum = xpos / (PIANO_KEY_XWID*7) * 12 + i;	/* calc key number */

  if (keynum > 127)
    keynum = 127;

  return (keynum);
}

static void
piano_destroy (GtkObject * object)
{
  Piano *piano;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_PIANO (object));

  piano = PIANO (object);

  gdk_pixmap_unref (piano->keyb_pm);
  gdk_pixmap_unref (piano->selw_pm);
  gdk_pixmap_unref (piano->unsw_pm);
  gdk_pixmap_unref (piano->selb_pm);
  gdk_pixmap_unref (piano->unsb_pm);
  gdk_gc_unref (piano->shadowgc);
  gdk_gc_unref (piano->c60gc);
  gdk_color_free (&piano->shadclr);
  gdk_color_free (&piano->c60clr);

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (*GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
piano_realize (GtkWidget * widget)
{
  Piano *piano;
  GdkWindowAttr attributes;
  gint attributes_mask;
  gint i, x, mod;
  GdkPixmap *pixmap;
  GdkColormap *cmap;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_PIANO (widget));

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
  piano = PIANO (widget);

  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.event_mask = gtk_widget_get_events (widget) |
    GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
    GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
    GDK_POINTER_MOTION_HINT_MASK;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  widget->window = gdk_window_new (widget->parent->window, &attributes,
    attributes_mask);

  widget->style = gtk_style_attach (widget->style, widget->window);

  gdk_window_set_user_data (widget->window, widget);

  gtk_style_set_background (widget->style, widget->window, GTK_STATE_ACTIVE);

  pixmap = gdk_pixmap_new (piano->widget.window, PIANO_DEFAULT_SIZEX, PIANO_DEFAULT_SIZEY, -1);
  gdk_draw_rectangle (pixmap, piano->widget.style->white_gc, TRUE, 1,
    1 + POFSY, 674, PIANO_DEFAULT_SIZEY-2);
  gdk_draw_line (pixmap, piano->widget.style->black_gc, 0, 0 + POFSY, PIANO_DEFAULT_SIZEX-1,
    0 + POFSY);
  gdk_draw_line (pixmap, piano->widget.style->black_gc, 0, PIANO_DEFAULT_SIZEY-1 + POFSY, PIANO_DEFAULT_SIZEX-1,
    PIANO_DEFAULT_SIZEY-1 + POFSY);
  gdk_draw_line (pixmap, piano->widget.style->black_gc, 1, PIANO_DEFAULT_SIZEY-4 + POFSY, PIANO_DEFAULT_SIZEX-2,
    PIANO_DEFAULT_SIZEY-4 + POFSY);

  /* allocate color and gc for piano key shadow */
  piano->shadowgc = gdk_gc_new (piano->widget.window);
  cmap = gtk_widget_get_colormap (widget);
  piano->shadclr.red = piano->shadclr.green = piano->shadclr.blue = 49152;
  gdk_colormap_alloc_color (cmap, &piano->shadclr, FALSE, TRUE);
  gdk_gc_set_foreground (piano->shadowgc, &piano->shadclr);

  /* allocate color and gc for piano key shadow */
  piano->blackpressgc = gdk_gc_new (piano->widget.window);
  cmap = gtk_widget_get_colormap (widget);
  piano->blackpressclr.red = 65535;
  piano->blackpressclr.green = 0;
  piano->blackpressclr.blue = 32768;
  gdk_colormap_alloc_color (cmap, &piano->blackpressclr, FALSE, TRUE);
  gdk_gc_set_foreground (piano->blackpressgc, &piano->blackpressclr);

  /* allocate color and gc for note C-60 marker */
  piano->c60gc = gdk_gc_new (piano->widget.window);
  piano->c60clr = c60clr;
  gdk_colormap_alloc_color (cmap, &piano->c60clr, FALSE, TRUE);
  gdk_gc_set_foreground (piano->c60gc, &piano->c60clr);

  gdk_draw_line (pixmap, piano->shadowgc, 1, PIANO_DEFAULT_SIZEY-3 + POFSY, PIANO_DEFAULT_SIZEX-2, PIANO_DEFAULT_SIZEY-3 + POFSY);
  gdk_draw_line (pixmap, piano->shadowgc, 1, PIANO_DEFAULT_SIZEY-2 + POFSY, PIANO_DEFAULT_SIZEX-2, PIANO_DEFAULT_SIZEY-2 + POFSY);

  for (i = 0, x = 0; i < 76; i++, x += PIANO_KEY_XWID)
    {
      gdk_draw_line (pixmap, piano->widget.style->black_gc, x, 1 + POFSY,
	x, PIANO_DEFAULT_SIZEY-2 + POFSY);
      mod = i % 7;
      if ((mod != 0) && (mod != 3) && (i != 75))
	gdk_draw_rectangle (pixmap, piano->widget.style->black_gc, TRUE,
	  x - 1, 1 + POFSY, PIANO_KEY_XWID/2+1, PIANO_DEFAULT_SIZEY/2);
    }

  /* draw note C-60 marker */
  gdk_draw_rectangle (pixmap, piano->c60gc, TRUE, PIANO_DEFAULT_SIZEX/2-PIANO_DEFAULT_SIZEX/33-1, PIANO_DEFAULT_SIZEY-13, PIANO_KEY_XWID*2/3, 4);
  //  gdk_draw_line (pixmap, piano->c60gc, 317, PIANO_DEFAULT_SIZEY*2/3+1, 317, PIANO_DEFAULT_SIZEY/3*2+3);
  //  gdk_draw_line (pixmap, piano->c60gc, 322, PIANO_DEFAULT_SIZEY*2/3+1, 322, PIANO_DEFAULT_SIZEY/3*2+3);

  piano->keyb_pm = pixmap;

  /* capture small pixmap of white key unselected state */
  piano->unsw_pm = gdk_pixmap_new (piano->widget.window, PIANO_KEY_XWID-1, 8, -1);
  gdk_draw_pixmap (piano->unsw_pm,
    piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
    pixmap, 1, PIANO_DEFAULT_SIZEY-8 + POFSY, 0, 0, PIANO_KEY_XWID-1, 8);

  /* copy and modify unselected white key state to get selected white key */
  piano->selw_pm = gdk_pixmap_new (piano->widget.window, PIANO_KEY_XWID-1, 8, -1);
  gdk_draw_pixmap (piano->selw_pm,
    piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
    piano->unsw_pm, 0, 0, 0, 0, PIANO_KEY_XWID-1, 8);
  gdk_draw_rectangle (piano->selw_pm, piano->blackpressgc, TRUE,
    PIANO_KEY_XWID/4, 0, PIANO_KEY_XWID/2, 4);
  gdk_draw_rectangle (piano->selw_pm, piano->widget.style->white_gc, TRUE,
    0, 4, PIANO_KEY_XWID-1, 3);

  /* capture small pixmap of black key unselected state */
  piano->unsb_pm = gdk_pixmap_new (piano->widget.window, PIANO_KEY_XWID/2+1, 8, -1);
  gdk_draw_pixmap (piano->unsb_pm,
    piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
    pixmap, PIANO_KEY_XWID-1,PIANO_DEFAULT_SIZEY/5 + POFSY, 0, 0, PIANO_KEY_XWID/2+1, 8);

  /* copy and modify unselected black key state to get selected black key */
  piano->selb_pm = gdk_pixmap_new (piano->widget.window, PIANO_KEY_XWID/2+1, 8, -1);
  gdk_draw_pixmap (piano->selb_pm,
    piano->widget.style->fg_gc[GTK_WIDGET_STATE (&piano->widget)],
    piano->unsb_pm, 0, 0, 0, 0, PIANO_KEY_XWID/2+1, 8);
  gdk_draw_rectangle (piano->selb_pm, piano->blackpressgc, TRUE,
    0, 0, PIANO_KEY_XWID/2+1, 5);
//  gdk_draw_line (piano->selb_pm, piano->widget.style->black_gc, 0, 7, 1, 7);
  gdk_draw_line (piano->selb_pm, piano->widget.style->black_gc, 0, 7, PIANO_KEY_XWID/2, 7);
}

static void
piano_size_request (GtkWidget * widget, GtkRequisition * requisition)
{
  requisition->width = PIANO_DEFAULT_SIZEX+5;
  requisition->height = PIANO_DEFAULT_SIZEY;
}

static void
piano_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
  Piano *piano;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_PIANO (widget));
  g_return_if_fail (allocation != NULL);

  widget->allocation = *allocation;
  piano = PIANO (widget);

  if (GTK_WIDGET_REALIZED (widget))
    {
      gdk_window_move_resize (widget->window, allocation->x, allocation->y,
	allocation->width, allocation->height);
    }
}

static gint
piano_expose (GtkWidget * widget, GdkEventExpose * event)
{
  Piano *piano;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (IS_PIANO (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  piano = PIANO (widget);

  gdk_draw_pixmap (widget->window,
    widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
    piano->keyb_pm, event->area.x, event->area.y,
    event->area.x, event->area.y, event->area.width, event->area.height);

  return FALSE;
}

