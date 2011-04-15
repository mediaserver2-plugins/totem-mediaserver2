#
# Makefile.am
#
# Copyright (C) 2011 Igalia S.L. All rights reserved.

#ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src

MAINTAINERCLEANFILES = \
        aclocal.m4 \
        compile \
	config.guess \
        config.sub \
        configure \
	depcomp \
	install-sh \
	ltmain.sh \
	missing \
	libtool \
	stamp-h1 \
	Makefile.in \
	*.make \
	mkinstalldirs \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)

#EXTRA_DIST = ./m4/introspection.m4 ./m4/silent.m4

#DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

distclean-local:
	-rm -rf autom4te.cache
