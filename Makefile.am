# 
#  Copyright 2005  Red Hat, Inc.
# 
#  Permission to use, copy, modify, distribute, and sell this software and its
#  documentation for any purpose is hereby granted without fee, provided that
#  the above copyright notice appear in all copies and that both that
#  copyright notice and this permission notice appear in supporting
#  documentation, and that the name of Red Hat not be used in
#  advertising or publicity pertaining to distribution of the software without
#  specific, written prior permission.  Red Hat makes no
#  representations about the suitability of this software for any purpose.  It
#  is provided "as is" without express or implied warranty.
# 
#  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
#  EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
#  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.

SUBDIRS = po

bin_PROGRAMS = desktop-effects

desktop_effects_CFLAGS = $(DEP_CFLAGS) -DDATADIR=\"$(pkgdatadir)\" -I$(top_srcdir) -DDESKTOPEFFECTSLOCALEDIR=\""$(desktopeffectslocaledir)"\"
desktop_effects_LDADD = $(DEP_LIBS)

desktop_effects_SOURCES =	\
        desktop-effects.c

desktop_effectsdir = $(pkgdatadir)
desktop_effects_DATA = desktop-effects.glade

desktopdir = $(datadir)/applications


desktop_in_files = desktop-effects.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

install-data-local:
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/16x16/apps/
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/24x24/apps/
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/32x32/apps/
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/36x36/apps/
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/96x96/apps/
	$(INSTALL_DATA) $(srcdir)/desktop-effects16.png $(DESTDIR)$(datadir)/icons/hicolor/16x16/apps/desktop-effects.png
	$(INSTALL_DATA) $(srcdir)/desktop-effects24.png $(DESTDIR)$(datadir)/icons/hicolor/24x24/apps/desktop-effects.png
	$(INSTALL_DATA) $(srcdir)/desktop-effects32.png $(DESTDIR)$(datadir)/icons/hicolor/32x32/apps/desktop-effects.png
	$(INSTALL_DATA) $(srcdir)/desktop-effects36.png $(DESTDIR)$(datadir)/icons/hicolor/36x36/apps/desktop-effects.png
	$(INSTALL_DATA) $(srcdir)/desktop-effects48.png $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/desktop-effects.png
	$(INSTALL_DATA) $(srcdir)/desktop-effects96.png $(DESTDIR)$(datadir)/icons/hicolor/96x96/apps/desktop-effects.png
	touch $(DESTDIR)$(datadir)/icons/hicolor

EXTRA_DIST = desktop-effects.desktop.in desktop-effects.glade	\
	desktop-effects16.png					\
	desktop-effects24.png					\
	desktop-effects32.png					\
	desktop-effects36.png					\
	desktop-effects48.png					\
	desktop-effects96.png					\
								\
	intltool-extract.in					\
	intltool-merge.in					\
	intltool-update.in

CLEANFILES =							\
	intltool-extract					\
	intltool-merge						\
	intltool-update						\
	desktop-effects.desktop

uninstall-hook:
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/16x16/apps/desktop-effects.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/24x24/apps/desktop-effects.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/32x32/apps/desktop-effects.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/36x36/apps/desktop-effects.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/desktop-effects.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/96x96/apps/desktop-effects.png

TAG = DESKTOP_EFFECTS_$(subst .,_,$(PACKAGE_VERSION))
tag:
	cvs tag -F $(TAG)
	make dist

