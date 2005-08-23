## Process this file with automake to produce Makefile.in

AM_CFLAGS = @ASEQVIEW_CFLAGS@

bin_PROGRAMS = aseqview
man_MANS = aseqview.1

aseqview_SOURCES = \
	aseqview.c \
	levelbar.c levelbar.h \
	piano.c piano.h \
	portlib.c portlib.h

aseqview_LDADD = @ASEQVIEW_LIBS@

EXTRA_DIST = \
	$(man_MANS) \
	bitmaps/gm.xbm bitmaps/gs.xbm bitmaps/xg.xbm etc/aseqview.spec.in \
	etc/aseqview.spec.in

CONFIG_CLEAN_FILES = etc/aseqview.spec

