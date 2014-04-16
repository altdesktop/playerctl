m4_define([playerctl_major_version], [0])
m4_define([playerctl_minor_version], [1])
m4_define([playerctl_micro_version], [0])

m4_define([playerctl_version], [playerctl_major_version.playerctl_minor_version.playerctl_micro_version])

AC_PREREQ(2.69)

AC_INIT([playerctl], [playerctl_version], [tony@dubstepdish.com])

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_SRCDIR([playerctl/playerctl.h])
AC_CONFIG_HEADER([config.h])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries
PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.38])
PKG_CHECK_MODULES([GIO], [gio-unix-2.0])

AC_PATH_PROG([GDBUS_CODEGEN], [`$PKG_CONFIG --variable=gdbus_codegen gio-2.0`])
if test -z "$GDBUS_CODEGEN"; then
    AC_MSG_ERROR([*** gdbus-codegen is required to build playerctl])
fi

# Checks for typedefs, structures, and compiler characteristics
AC_PROG_CC_STDC

# Compiler flags
PLAYERCTL_CFLAGS="$GOBJECT_CFLAGS $GIO_CFLAGS"
AC_SUBST(PLAYERCTL_CFLAGS)

PLAYERCTL_LIBS="$GOBJECT_LIBS $GIO_LIBS"
AC_SUBST(PLAYERCTL_LIBS)

AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])
AM_PATH_GLIB_2_0
AM_PROG_AR
AM_SILENT_RULES([yes])
LT_INIT

# Versioning
PLAYERCTL_MAJOR_VERSION=playerctl_major_version
PLAYERCTL_MINOR_VERSION=playerctl_minor_version
PLAYERCTL_MICRO_VERSION=playerctl_micro_version
PLAYERCTL_VERSION=playerctl_version
AC_SUBST(PLAYERCTL_MAJOR_VERSION)
AC_SUBST(PLAYERCTL_MINOR_VERSION)
AC_SUBST(PLAYERCTL_MICRO_VERSION)
AC_SUBST(PLAYERCTL_VERSION)

# Introspection
GOBJECT_INTROSPECTION_CHECK([1.38.0])

# Check for gtk-doc
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

# Output
AC_CONFIG_FILES([Makefile
                 playerctl/Makefile
                 doc/Makefile
                 doc/reference/Makefile
                 doc/reference/version.xml
                 ])

AC_OUTPUT
