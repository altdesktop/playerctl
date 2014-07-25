# preamble

NULL =
AM_CPPFLAGS =
AM_CFLAGS =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST =
DISTCLEANFILES =
lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CPPFLAGS += \
	-DPLAYERCTL_COMPILATION \
	$(PLAYERCTL_CFLAGS) \
	$(NULL)

AM_CFLAGS += \
	$(PLAYERCTL_CFLAGS) \
	$(NULL)

source_h = \
	$(top_srcdir)/playerctl/playerctl-player.h \
	$(NULL)

source_c = \
	playerctl-player.c \
	$(NULL)

DISTCLEANFILES += playerctl-version.h

playerctl-generated.c:
	$(GDBUS_CODEGEN) --generate-c-code playerctl-generated mpris-dbus-interface.xml

BUILT_SOURCES += \
	playerctl-generated.c \
	playerctl-generated.h \
	$(NULL)

CLEANFILES += $(BUILT_SOURCES)

lib_LTLIBRARIES += libplayerctl-1.0.la
libplayerctl_1_0_la_LIBADD = $(PLAYERCTL_LIBS)
libplayerctl_1_0_la_SOURCES = $(source_c) $(source_h) $(BUILT_SOURCES)

playerctlincludedir = $(includedir)/playerctl
playerctlinclude_DATA = \
	$(source_h) \
	$(top_srcdir)/playerctl/playerctl-version.h \
	$(top_srcdir)/playerctl/playerctl.h \
	$(NULL)

EXTRA_DIST += playerctl.h playerctl-version.h.in playerctl-version.h

pcfiles = playerctl-1.0.pc

playerctl-1.0.pc: playerctl.pc
	$(AM_V_GEN)cp -f $< $@

pkgconfig_DATA = $(pcfiles)
pkgconfigdir = $(libdir)/pkgconfig
CLEANFILES += $(pcfiles)
EXTRA_DIST += playerctl.pc.in

bin_PROGRAMS += playerctl
playerctl_SOURCES = playerctl-cli.c
playerctl_CPPFLAGS = $(AM_CPPFLAGS)
playerctl_LDADD = libplayerctl-1.0.la $(PLAYERCTL_LIBS)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = Playerctl_1_0_gir
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS =

if HAVE_INTROSPECTION

Playerctl-1.0.gir: libplayerctl-1.0.la
Playerctl_1_0_gir_INCLUDES = GObject-2.0
Playerctl_1_0_gir_CFLAGS = $(AM_CPPFLAGS)
Playerctl_1_0_gir_PACKAGES =
Playerctl_1_0_gir_LIBS = libplayerctl-1.0.la
Playerctl_1_0_gir_FILES = $(source_h) $(source_c)
Playerctl_1_0_gir_NAMESPACE = Playerctl
INTROSPECTION_GIRS += Playerctl-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif
