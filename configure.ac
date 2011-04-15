#
# configure.ac
# 
#
# Copyright (C) 2011 Igalia S.L.. All rights reserved.

AC_INIT([totem-mediaserver2], [0.0.1])

AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src])

AM_CONFIG_HEADER(src/config.h)

AC_PROG_CC
AC_PROG_INSTALL

LT_PREREQ([2.2.6])
LT_INIT

AM_PROG_CC_C_O

AC_HEADER_STDC

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# ----------------------------------------------------------
# ENVIRONMENT CONFIGURATION
# ----------------------------------------------------------

GNOME_COMPILE_WARNINGS([maximum])
CFLAGS+=" ${WARN_CFLAGS} -Wmissing-declarations -std=c99 "

GNOME_MAINTAINER_MODE_DEFINES

# ----------------------------------------------------------
# DEFINITIONS
# ----------------------------------------------------------

# Give parameters a default value

if test $prefix = "NONE" ; then
	prefix="/usr/local"
fi

if test $libdir = "\${exec_prefix}/lib" ; then
	libdir="$prefix/lib"
fi

if test $datadir = "NONE" ; then
	datadir="$prefix/share"
fi

# ----------------------------------------------------------
# DEPENDENCIES
# ----------------------------------------------------------

PKG_CHECK_MODULES(DEPS, mediaserver2    \
                        libpeas-1.0     \
                        totem)

# ----------------------------------------------------------
# PLUGINS
# ----------------------------------------------------------

PLUGINS_DIR=`pkg-config --variable=pluginsdir totem`
AC_SUBST(PLUGINS_DIR)
AC_DEFINE_UNQUOTED(PLUGINS_DIR, "$PLUGINS_DIR", [Plugins directory])

# ----------------------------------------------------------
# DEBUG SUPPORT
# ----------------------------------------------------------

AC_ARG_ENABLE(debug,
        [  --enable-debug include debug symbols],,
        enable_debug=no)
if test "x$enable_debug" = "xyes"; then
        AC_MSG_WARN("Configuring with debugging options enabled!")
	CFLAGS="$CFLAGS -g3 -O0"
fi

# ----------------------------------------------------------
# GETTEXT
# ----------------------------------------------------------

GETTEXT_PACKAGE=totem-grilo
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])

AM_GLIB_GNU_GETTEXT

# ----------------------------------------------------------
# WORKAROUNDS
# ----------------------------------------------------------

AC_SUBST([abs_top_builddir])
AC_SUBST([builddir])

# ----------------------------------------------------------
# OUTPUT
# ----------------------------------------------------------

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  src/mediaserver2.plugin
])

AC_OUTPUT
