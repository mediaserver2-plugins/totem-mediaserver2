modules_flags =	-export_dynamic -avoid-version -module

plugindir =		$(PLUGINDIR)/mediaserver1
plugin_LTLIBRARIES =	libmediaserver1.la

uidir =	$(plugindir)

plugin_in_files =	mediaserver1.totem-plugin.in

%.totem-plugin: %.totem-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA =	$(plugin_in_files:.totem-plugin.in=.totem-plugin)

common_defines =						\
	-D_REENTRANT						\
	-DDBUS_API_SUBJECT_TO_CHANGE				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DGCONF_PREFIX=\""/apps/totem"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DBINDIR=\""$(bindir)"\"				\
	-DTOTEM_PLUGIN_DIR=\""$(libdir)/totem/plugins"\"	\
	$(DISABLE_DEPRECATED)

libmediaserver1_la_SOURCES =	totem-mediaserver1.c
libmediaserver1_la_LDFLAGS =	$(modules_flags)
libmediaserver1_la_CPPFLAGS =	$(common_defines)
libmediaserver1_la_LIBADD =	$(LIBMS1_LIBS)

libmediaserver1_la_CFLAGS =		\
	$(DEPENDENCY_CFLAGS)		\
	$(LIBMS1_CFLAGS)		\
	$(WARN_CFLAGS)			\
	$(DBUS_CFLAGS)			\
	$(UNIQUE_CFLAGS)		\
	$(AM_CFLAGS)			\
	-I$(top_srcdir)/		\
	-I$(top_srcdir)/src		\
        -I$(top_srcdir)/lib     	\
        -I$(top_srcdir)/src/backend	\
	-I$(top_srcdir)/src/plugins

EXTRA_DIST =	$(plugin_in_files) $(ui_DATA)

CLEANFILES =		$(plugin_DATA) $(BUILT_SOURCES)
DISTCLEANFILES =	$(plugin_DATA)
