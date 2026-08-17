/*==================================================================
 * piano.h - Header file for piano widget
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
 *
 * To contact the author of this program:
 * Email: Josh Green <jgreen@users.sourceforge.net>
 * Swami homepage: http://swami.sourceforge.net
 *==================================================================*/
#ifndef __PIANO_WIDG_H__
#define __PIANO_WIDG_H__

#include <gtk/gtk.h>

#define PIANO(obj)       G_TYPE_CHECK_INSTANCE_CAST(obj, piano_get_type(), Piano)
#define PIANO_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST(klass, piano_get_type(), PianoClass)
#define IS_PIANO(obj)    G_TYPE_CHECK_INSTANCE_TYPE(obj, piano_get_type())

#define PIANO_KEY_XWID          5
#define PIANO_DEFAULT_SIZEX	(PIANO_KEY_XWID*76-PIANO_KEY_XWID+1)
#define PIANO_DEFAULT_SIZEY	20

typedef struct _Piano Piano;
typedef struct _PianoClass PianoClass;

struct _Piano
{
  GtkWidget widget;

  cairo_surface_t *keyb_surface;	/* backing surface for the full keyboard */

  gboolean *selkeys;		/* array of 128 boolean flags of active keys */
};

struct _PianoClass
{
  GtkWidgetClass parent_class;

  void (*note_on) (Piano * piano, guint keynum);
  void (*note_off) (Piano * piano, guint keynum);
};

GtkWidget *piano_new (gboolean * selkeys);
GType piano_get_type (void);
void piano_note_on (Piano * piano, guint8 keynum);
void piano_note_off (Piano * piano, guint8 keynum);

#endif
