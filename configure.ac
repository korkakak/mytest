dnl  Copyright 2006 Red Hat, Inc.
dnl 
dnl  Permission to use, copy, modify, distribute, and sell this software and its
dnl  documentation for any purpose is hereby granted without fee, provided that
dnl  the above copyright notice appear in all copies and that both that
dnl  copyright notice and this permission notice appear in supporting
dnl  documentation, and that the name of Red Hat not be used in
dnl  advertising or publicity pertaining to distribution of the software without
dnl  specific, written prior permission.  Red Hat makes no
dnl  representations about the suitability of this software for any purpose.  It
dnl  is provided "as is" without express or implied warranty.
dnl 
dnl  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
dnl  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
dnl  EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
dnl  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
dnl  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
dnl  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
dnl  PERFORMANCE OF THIS SOFTWARE.
dnl
dnl Process this file with autoconf to create configure.

AC_PREREQ([2.57])
AC_INIT(desktop-effects,[0.7.0])
AM_INIT_AUTOMAKE([dist-bzip2])
AM_MAINTAINER_MODE

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_INTLTOOL([0.35])

GETTEXT_PACKAGE=desktop-effects
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Name of default gettext domain])

ALL_LINGUAS="as bn_IN ca de el es fr gu hi hr it ja kn ko ml mr nb or pa pl pt_BR ru si sr@Latn sr ta te ur zh_CN zh_TW"
AM_GLIB_GNU_GETTEXT

desktopeffectslocaledir='${prefix}/${DATADIRNAME}/locale'
AC_SUBST(desktopeffectslocaledir)

# Checks for pkg-config packages
PKG_CHECK_MODULES(DEP, gconf-2.0 gtk+-2.0 libglade-2.0 xcomposite)
AC_SUBST(DEP_CFLAGS)
AC_SUBST(DEP_LIBS)

AC_OUTPUT([Makefile po/Makefile.in])
