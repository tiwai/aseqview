## Process this file with automake to produce Makefile.in

INCLUDES = \
	@GTK_CFLAGS@

bin_PROGRAMS = aseqview
man_MANS = aseqview.1

aseqview_SOURCES = \
	aseqview.c \
	levelbar.c levelbar.h \
	portlib.c portlib.h

aseqview_LDADD = @GTK_LIBS@

EXTRA_DIST = \
	bitmaps/gm.xbm bitmaps/gs.xbm bitmaps/xg.xbm etc/aseqview.spec.in

CONFIG_CLEAN_FILES = etc/aseqview.spec

