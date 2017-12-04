plugindir = $(libdir)/xfce4/panel/plugins
plugin_LTLIBRARIES = libappletclock.la

libappletclock_la_CFLAGS =						\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"			\
	@LIBXFCE4PANEL_CFLAGS@					\
	@LIBXFCE4UI_CFLAGS@ -g

libappletclock_LDFLAGS = 						\
	-avoid-version 						\
	-module 						\
	-no-undefined 						\
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)'

libappletclock_la_LIBADD =						\
	@LIBXFCE4PANEL_LIBS@					\
	@LIBXFCE4UI_LIBS@

libappletclock_la_SOURCES =		\
	clock.c

desktopdir = $(datadir)/xfce4/panel/plugins
desktop_DATA = applet-clock.desktop

EXTRA_DIST = 								\
	applet-clock.desktop.in

DISTCLEANFILES =							\
	$(desktop_DATA)

%.desktop: %.desktop.in
	cp $< $@
