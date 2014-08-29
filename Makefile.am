## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"\
	$(GDKLIB_CFLAGS)\
	 -Wall\
	 -g

bin_PROGRAMS = sbxkb

sbxkb_SOURCES = \
	main.c

sbxkb_LDFLAGS = -lX11

sbxkb_LDADD = \
	$(GDKLIB_LIBS)

EXTRA_DIST = \
	$(flags_DATA)

SUBDIRS = \
	images

