## Process this file with automake to produce Makefile.in

AM_CFLAGS = @ASEQVIEW_CFLAGS@

bin_PROGRAMS = aseqview
man_MANS = aseqview.1

aseqview_SOURCES = \
	aseqview.c tmprbits.h \
	levelbar.c levelbar.h \
	piano.c piano.h \
	portlib.c portlib.h

aseqview_LDADD = @ASEQVIEW_LIBS@

EXTRA_DIST = \
	$(man_MANS) \
	bitmaps/gm.xbm bitmaps/gs.xbm bitmaps/xg.xbm \
	bitmaps/blank1.xbm \
	bitmaps/Ces.xbm bitmaps/Ges.xbm bitmaps/Des.xbm bitmaps/As.xbm \
	bitmaps/Es.xbm bitmaps/B.xbm bitmaps/F.xbm bitmaps/C.xbm \
	bitmaps/G.xbm bitmaps/D.xbm bitmaps/A.xbm bitmaps/E.xbm \
	bitmaps/H.xbm bitmaps/Fis.xbm bitmaps/Cis.xbm \
	bitmaps/As_m.xbm bitmaps/Es_m.xbm bitmaps/B_m.xbm bitmaps/F_m.xbm \
	bitmaps/C_m.xbm bitmaps/G_m.xbm bitmaps/D_m.xbm bitmaps/A_m.xbm \
	bitmaps/E_m.xbm bitmaps/H_m.xbm bitmaps/Fis_m.xbm bitmaps/Cis_m.xbm \
	bitmaps/Gis_m.xbm bitmaps/Dis_m.xbm bitmaps/Ais_m.xbm \
	bitmaps/blank2.xbm \
	bitmaps/eq.xbm bitmaps/Py.xbm bitmaps/mt.xbm bitmaps/pu.xbm \
	bitmaps/u0.xbm bitmaps/u1.xbm bitmaps/u2.xbm bitmaps/u3.xbm \
	etc/aseqview.spec.in

CONFIG_CLEAN_FILES = etc/aseqview.spec

