#
# Makefile.am
#
# Copyright (C) 2011 Igalia S.L. All rights reserved.

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugin_LTLIBRARIES = libmediaserver2.la

plugindir = $(PLUGINS_DIR)/mediaserver2

plugin_in_files = mediaserver2.plugin.in

libmediaserver2_la_SOURCES = totem-mediaserver2.c

libmediaserver2_la_CFLAGS = $(DEPS_CFLAGS)

libmediaserver2_la_LIBADD = $(DEPS_LIBS)

libmediaserver2_la_LDFLAGS =	\
	-export-dynamic		\
	-avoid-version		\
	-module

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
EXTRA_DIST = $(plugin_in_files)
