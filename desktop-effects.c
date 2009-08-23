/* -*- mode: C; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */
/*
    Desktop Effects. A preference panel for compiz.
    Copyright (C) 2006   Red Hat, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Author: Soren Sandmann (sandmann@redhat.com) */

#include <config.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <gconf/gconf-value.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#define PLUGIN_LIST_KEY		"/apps/compiz/general/allscreens/options/active_plugins"
#define WINDOW_MANAGER_KEY	"/desktop/gnome/session/required_components/windowmanager"

typedef struct App App;

typedef enum
{
    COMPIZ,
    METACITY
} WindowManager;

typedef struct
{
    gboolean	enabled;
    gboolean	cube;
    gboolean	wobbly;
} Settings;

struct App
{
    GtkWidget	       *dialog;
    GtkToggleButton    *enable;
    GtkToggleButton    *wobbly;
    GtkToggleButton    *cube;
    WindowManager	currently_running;
    
    gboolean		enabled_at_startup;
    
    Settings		initial;
    
    GConfClient	       *gconf;
    
    gboolean		compiz_running;
};

static void
update_app (App *app)
{
    gboolean sensitive = gtk_toggle_button_get_active (app->enable);
    
    gtk_widget_set_sensitive (GTK_WIDGET (app->wobbly), sensitive);
    gtk_widget_set_sensitive (GTK_WIDGET (app->cube), sensitive);
}

static WindowManager
current_configured_wm (App *app,
		       GError **err)
{
    GError *tmp = NULL;
    
    const char *str = gconf_client_get_string (app->gconf,
					       WINDOW_MANAGER_KEY,
					       &tmp);
    
    if (tmp)
    {
	g_propagate_error (err, tmp);
	return METACITY;
    }
    
    if (str && strcmp (str, "compiz-gtk") == 0)
    {
	return COMPIZ;
    }
    else
    {
	return METACITY;
    }
}


#define REVERT_COUNT 40

/* Returns TRUE if the settings should actually be applied. 
 */

static void
show_error (const GError *err)
{
    if (!err)
	return;
    
    GtkWidget *dialog = gtk_message_dialog_new (
	NULL,
	GTK_DIALOG_DESTROY_WITH_PARENT,
	GTK_MESSAGE_WARNING,
	GTK_BUTTONS_OK, err->message);
    
    gtk_window_set_title (GTK_WINDOW (dialog), "");
    
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

struct TimeoutData
{
    int time;
    GtkLabel *label;
    GtkDialog *dialog;
    gboolean timed_out;
};

static gboolean
free_at_idle (gpointer data)
{
    g_free (data);
    return FALSE;
}

static char *
idle_free (char *str)
{
    g_idle_add (free_at_idle, str);
    return str;
}

static char *
timeout_string (int time)
{
    char *str = g_strdup_printf (ngettext ("Testing the new settings. If you don't respond in %d second the previous settings will be restored.", "Testing the new settings. If you don't respond in %d seconds the previous settings will be restored.", time), time);
    
    return idle_free (str);
}

static gboolean
save_timeout_callback (gpointer _data)
{
    struct TimeoutData *data = _data;
    
    data->time--;
    
    if (data->time == 0)
    {
	gtk_dialog_response (data->dialog, GTK_RESPONSE_NO);
	data->timed_out = TRUE;
	return FALSE;
    }
    
    gtk_label_set_text (data->label, timeout_string (data->time));
    
    return TRUE;
}

static gboolean
run_timed_dialog (App *app)
{
    GtkWidget *dialog;
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *label_sec;
    GtkWidget *image;
    int res;
    struct TimeoutData timeout_data;
    guint timeout;
    
    dialog = gtk_dialog_new ();
    gtk_window_set_transient_for (
	GTK_WINDOW (dialog), GTK_WINDOW (app->dialog));
    gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
    gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
    gtk_window_set_title (GTK_WINDOW (dialog), _("Keep Settings"));
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ALWAYS);
    
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), idle_free (
			      g_strdup_printf ("<b>%s</b>",
					       _("Do you want to keep these settings?"))));
    image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
    gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
    
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    
    label_sec = gtk_label_new (timeout_string (REVERT_COUNT));
    gtk_label_set_line_wrap (GTK_LABEL (label_sec), TRUE);
    gtk_label_set_selectable (GTK_LABEL (label_sec), TRUE);
    gtk_misc_set_alignment (GTK_MISC (label_sec), 0.0, 0.5);
    
    hbox = gtk_hbox_new (FALSE, 6);
    vbox = gtk_vbox_new (FALSE, 6);
    
    gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), label_sec, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, FALSE, FALSE, 0);
    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			    _("Use _previous settings"), GTK_RESPONSE_NO,
			    _("_Keep settings"), GTK_RESPONSE_YES, NULL);
    
    gtk_widget_show_all (hbox);
    
    timeout_data.time = REVERT_COUNT;
    timeout_data.label = GTK_LABEL (label_sec);
    timeout_data.dialog = GTK_DIALOG (dialog);
    timeout_data.timed_out = FALSE;
    
    timeout = g_timeout_add (1000, save_timeout_callback, &timeout_data);
    res = gtk_dialog_run (GTK_DIALOG (dialog));
    
    if (!timeout_data.timed_out)
	g_source_remove (timeout);
    
    gtk_widget_destroy (dialog);
    
    return (res == GTK_RESPONSE_YES);
}

static gboolean
start_compiz (App *app, GError **err)
{
    if (!g_spawn_command_line_async ("compiz-gtk --replace", err))
	return FALSE;
    
    app->compiz_running = TRUE;
    
    return TRUE;
}

static gboolean
start_metacity (App *app, GError **err)
{
    if (!g_spawn_command_line_async ("metacity --replace", err))
	return FALSE;
    
    app->compiz_running = FALSE;
    
    return TRUE;
}

static void
get_widget_settings (App *app,
		     Settings *settings)
{
    if (!settings)
	return;
    
    settings->enabled = gtk_toggle_button_get_active (app->enable);
    settings->cube = gtk_toggle_button_get_active (app->cube);
    settings->wobbly = gtk_toggle_button_get_active (app->wobbly);
}

static void
apply_settings (App *app, Settings *settings)
{
    const char *str = settings->enabled? "compiz-gtk" : "metacity";
    char *session_file;
    
    gconf_client_set_string (app->gconf,
			     WINDOW_MANAGER_KEY,
			     str, NULL);
    
    session_file = g_build_filename (g_get_home_dir (),
				     ".gnome2", "session", NULL);
    g_unlink (session_file);
    g_free (session_file);
}

static void
set_busy (GtkWidget *widget, gboolean busy)
{
    GdkCursor *cursor;
    
    if (busy)
	cursor = gdk_cursor_new (GDK_WATCH);
    else
	cursor = NULL;
    
    gdk_window_set_cursor (widget->window, cursor);
    
    if (cursor)
	gdk_cursor_unref (cursor);
    
    gdk_flush ();
}

/* get_wm_window() and current_window_manager() are essentially cutted and pasted
 * from gnome-wm.c from gnome-control-center.
 */
static Window
get_wm_window (void)
{
    Window *xwindow;
    Atom type;
    gint format;
    gulong nitems;
    gulong bytes_after;
    Window result;
    
    XGetWindowProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
			XInternAtom (GDK_DISPLAY (), "_NET_SUPPORTING_WM_CHECK", False),
			0, G_MAXLONG, False, XA_WINDOW, &type, &format,
			&nitems, &bytes_after, (guchar **) &xwindow);
    
    if (type != XA_WINDOW)
    {
	return None;
    }
    
    gdk_error_trap_push ();
    XSelectInput (GDK_DISPLAY (), *xwindow, StructureNotifyMask | PropertyChangeMask);
    XSync (GDK_DISPLAY (), False);
    
    if (gdk_error_trap_pop ())
    {
	XFree (xwindow);
	return None;
    }
    
    result = *xwindow;
    XFree (xwindow);
    
    return result;
}

static char*
get_current_window_manager (void)
{
    Atom utf8_string, atom, type;
    int result;
    char *retval;
    int format;
    gulong nitems;
    gulong bytes_after;
    guchar *val;
    Window wm_window = get_wm_window ();
    
    utf8_string = XInternAtom (GDK_DISPLAY (), "UTF8_STRING", False);
    atom = XInternAtom (GDK_DISPLAY (), "_NET_WM_NAME", False);
    
    gdk_error_trap_push ();
    
    result = XGetWindowProperty (GDK_DISPLAY (),
				 wm_window,
				 atom,
				 0, G_MAXLONG,
				 False, utf8_string,
				 &type, &format, &nitems,
				 &bytes_after, (guchar **)&val);
    
    if (gdk_error_trap_pop () || result != Success)
	return NULL;
    
    if (type != utf8_string ||
	format !=8 ||
	nitems == 0)
    {
	if (val)
	    XFree (val);
	return NULL;
    }
    
    if (!g_utf8_validate ((char *)val, nitems, NULL))
    {
	XFree (val);
	return NULL;
    }
    
    retval = g_strndup ((char *)val, nitems);
    
    XFree (val);
    
    return retval;
}

static gboolean
compiz_started (void)
{
    gboolean result;
    char *wm = get_current_window_manager ();
    
    result = wm && strcmp (wm, "compiz") == 0;
    
    g_free (wm);
    
    return result;
}

typedef struct TimedDialogInfo
{
    App *app;
    GTimer *timer;
    Settings settings;
} TimedDialogInfo;

#define SECONDS_WE_WILL_WAIT_FOR_COMPIZ_TO_START 8

static gboolean
show_dialog_timeout (gpointer data)
{
    TimedDialogInfo *info = data;
    gboolean has_compiz;
    
    gtk_window_present (GTK_WINDOW (info->app->dialog));
    
    has_compiz = compiz_started();
    
    if (has_compiz || g_timer_elapsed (info->timer, NULL) > SECONDS_WE_WILL_WAIT_FOR_COMPIZ_TO_START)
    {
	if (has_compiz)
	{
	    set_busy (info->app->dialog, FALSE);
	
	    if (run_timed_dialog (info->app))
		apply_settings (info->app, &info->settings);
	    else
		gtk_toggle_button_set_active (info->app->enable, FALSE);
	}
	else
	{
	    GtkWidget *dialog;

	    gtk_toggle_button_set_active (info->app->enable, FALSE);

	    set_busy (info->app->dialog, FALSE);
	
	    dialog = gtk_message_dialog_new (
		(GtkWindow *)info->app->dialog,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_WARNING,
		GTK_BUTTONS_OK, "Desktop effects could not be enabled");
	    
	    gtk_window_set_title (GTK_WINDOW (dialog), "");
	    gtk_dialog_run (GTK_DIALOG (dialog));
	    gtk_widget_destroy (dialog);
	}
	
	gtk_widget_set_sensitive (info->app->dialog, TRUE);
	
	g_timer_destroy (info->timer);
	g_free (info);
	
	return FALSE;
    }
    
    return TRUE;
}

static void
on_enable_toggled (GtkWidget *widget,
		   gpointer data)
{
    App *app = data;
    Settings settings;
    
    get_widget_settings (app, &settings);
    
    if (settings.enabled != app->compiz_running)
    {
	GError *err = NULL;
	
	if (settings.enabled)
	{
	    start_compiz (app, &err);
	}
	else
	{
	    apply_settings (app, &settings);
	    start_metacity (app, &err);
	}
	
	if (err)
	{
	    show_error (err);
	    
	    g_signal_handlers_block_by_func (widget, on_enable_toggled, app);
	    
	    /* block the toggle signal */
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
					  !settings.enabled);
	    
	    g_signal_handlers_unblock_by_func (widget, on_enable_toggled, app);
	}
	else
	{
	    if (settings.enabled)
	    {
		TimedDialogInfo *info = g_new0 (TimedDialogInfo, 1);
		
		info->settings = settings;
		info->app = app;
		info->timer = g_timer_new ();
		
		set_busy (info->app->dialog, TRUE);
		gtk_widget_set_sensitive (app->dialog, FALSE);
		
		g_timeout_add (250, show_dialog_timeout, info);
	    }
	}
    }
    
    update_app (app);
}

static GSList *
get_plugins (App *app,
	     GError **err)
{
    return gconf_client_get_list (app->gconf,
				  PLUGIN_LIST_KEY,
				  GCONF_VALUE_STRING,
				  err);
}

static gboolean
contains_string (GSList *plugins,
		 const gchar *needle)
{
    GSList *slist;
    
    for (slist = plugins; slist != NULL; slist=slist->next)
    {
	const char *s = slist->data;
	
	if (strcmp (s, needle) == 0)
	    return TRUE;
    }
    
    return FALSE;
}

static gboolean
update_plugins (App     *app,
		GError **err)
{
    GError *tmp = NULL;
    GSList *plugins;
    Settings settings;
    GSList *new_setting = NULL;
    GSList *old;
    
    get_widget_settings (app, &settings);
    
    plugins = get_plugins (app, &tmp);
    
    if (tmp)
    {
	g_propagate_error (err, tmp);
	return FALSE;
    }
    
    /* Disable/enable the plugins that control the features we care
     * about.  Try no to assume too much about what plugins are loaded
     * in what order.
     */
    
    for (old = plugins; old != NULL; old = old->next)
    {
      char *name = old->data;

      if (strcmp (name, "cube") == 0 ||
	  strcmp (name, "rotate") == 0 ||
	  strcmp (name, "zoom") == 0 ||
	  strcmp (name, "wall") == 0 ||
	  strcmp (name, "wobbly") == 0)
	continue;

      new_setting = g_slist_prepend (new_setting, name);

      if (strcmp (name, "decoration") == 0)
      {
	if (settings.wobbly)
	  new_setting = g_slist_prepend (new_setting, "wobbly");
      }
      else if (strcmp (name, "minimize") == 0)
      {
	if (settings.cube)
	{
	  new_setting = g_slist_prepend (new_setting, "cube");
	  new_setting = g_slist_prepend (new_setting, "rotate");
	  new_setting = g_slist_prepend (new_setting, "zoom");
	}
	else
	  new_setting = g_slist_prepend (new_setting, "wall");
      }

    }
    
    new_setting = g_slist_reverse (new_setting);
    
    gconf_client_set_list (app->gconf, PLUGIN_LIST_KEY,
			   GCONF_VALUE_STRING, new_setting, &tmp);
    if (tmp)
    {
	g_propagate_error (err, tmp);
	return FALSE;
    }
    
    return TRUE;
}

static void
on_option_toggled (GtkWidget *widget,
		   App       *app)
{
    update_plugins (app, NULL);
}

static gboolean
get_gconf_settings (App *app,
		    Settings *settings,
		    GError **err)
{
    Settings result;
    WindowManager wm;
    GError *tmp = NULL;
    GSList *plugins;
    
    wm = current_configured_wm (app, &tmp);
    if (tmp)
    {
	g_propagate_error (err, tmp);
	return FALSE;
    }
    
    plugins = get_plugins (app, &tmp);
    if (tmp)
    {
	g_propagate_error (err, tmp);
	return FALSE;
    }
    
    result.enabled = (wm == COMPIZ);
    result.cube = contains_string (plugins, "cube");
    result.wobbly = contains_string (plugins, "wobbly");
    
    *settings = result;
    
    return TRUE;
}

static void
set_widgets (App *app,
	     const Settings *settings)
{
    gtk_toggle_button_set_active (app->enable, settings->enabled);
    gtk_toggle_button_set_active (app->cube, settings->cube);
    gtk_toggle_button_set_active (app->wobbly, settings->wobbly);
}

static gboolean
init_app (App *app,
	  GError **err)
{
#define GLADE_FILE DATADIR "desktop-effects.glade"
    GladeXML *xml;
    
    app->gconf = gconf_client_get_default ();
    
    xml = glade_xml_new (GLADE_FILE, NULL, NULL);
    
    if (!xml)
    {
	g_warning ("Could not open " GLADE_FILE);
	return FALSE;
    }
    
    app->dialog = glade_xml_get_widget (xml, "dialog");
    app->enable = GTK_TOGGLE_BUTTON (
	glade_xml_get_widget (xml, "enable_togglebutton"));
    app->cube   = GTK_TOGGLE_BUTTON (
	glade_xml_get_widget (xml, "cube_checkbox"));
    app->wobbly = GTK_TOGGLE_BUTTON (
	glade_xml_get_widget (xml, "wobble_checkbox"));
    
    g_signal_connect (app->enable, "toggled",
		      G_CALLBACK (on_enable_toggled), app);
    g_signal_connect (app->wobbly, "toggled",
		      G_CALLBACK (on_option_toggled), app);
    g_signal_connect (app->cube, "toggled",
		      G_CALLBACK (on_option_toggled), app);
    
    if (!get_gconf_settings (app, &(app->initial), err))
	return FALSE;
    
    /* We assume here that the user has not in the meantime started
     * a *third* window manager. Ie., compiz is running initially
     * if and only if "enabled" was true initially.
     */
    app->compiz_running = app->initial.enabled;
    
    set_widgets (app, &(app->initial));
    
    update_app (app);
    
    return TRUE;
}

static gboolean
has_composite ()
{
    int dummy1, dummy2;

    if (XCompositeQueryExtension (GDK_DISPLAY (), &dummy1, &dummy2))
	return TRUE;

    return FALSE;
}

static void
show_alert (const char *text)
{
    GtkWidget *dialog;
    
    dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
				     GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_OK,
				     text);
    
    gtk_dialog_run (GTK_DIALOG (dialog));
}

int
main (int argc, char **argv)
{
    App *app;
    GError *err = NULL;
    
    bindtextdomain (GETTEXT_PACKAGE, DESKTOPEFFECTSLOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    
    gtk_init (&argc, &argv);
    
    if (!has_composite())
    {
	show_alert ("The Composite extension is not available");
	return 0;
    }

    app = g_new0 (App, 1);
    
    if (!init_app (app, &err))
    {
	show_error (err);
	return 0;
    }

    gtk_window_set_icon_name (GTK_WINDOW (app->dialog), "desktop-effects");
    
    gtk_dialog_run (GTK_DIALOG (app->dialog));
    
    return 0;
}
