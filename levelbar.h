/*
 * levelbar.h
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

#ifndef LEVELBAR_H_DEF
#define LEVELBAR_H_DEF

#include <gtk/gtk.h>

GtkWidget *level_bar_new(int width, int height, int minval, int maxval, int curval);
GtkWidget *solid_bar_new(int width, int height, int minval, int maxval, int curval, int delayed);
GtkWidget *arrow_bar_new(int width, int height, int minval, int maxval, int curval, int delayed);
void channel_status_bar_update(GtkWidget *w, int val);
void channel_status_bar_set_color_rgb(GtkWidget *w, int r, int g, int b);
void level_bar_set_level_color_rgb(GtkWidget *w, int r, int g, int b);

void alloc_color(GdkColor *color, int red, int green, int blue);

#endif
