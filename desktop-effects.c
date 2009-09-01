/* -*- mode: C; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */
/*
    Desktop Effects. A preference panel for GNOME window management and effects
    Copyright (C) 2006-2009   Red Hat, Inc.

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
#include <GL/gl.h>
#include <GL/glx.h>

#define PLUGIN_LIST_KEY		"/apps/compiz/general/allscreens/options/active_plugins"
#define WINDOW_MANAGER_KEY	"/desktop/gnome/session/required_components/windowmanager"

typedef struct App App;

typedef enum
{
    COMPIZ,
    METACITY,
    GNOME_SHELL,
    UNKNOWN
} WindowManager;

typedef struct
{
    WindowManager window_manager;
    gboolean	  cube;
    gboolean	  wobbly;
} Settings;

struct App
{
    GtkWidget	       *dialog;
    GtkRadioButton     *standard;
    GtkRadioButton     *compiz;
    GtkRadioButton     *gnome_shell;
    GtkToggleButton    *wobbly;
    GtkToggleButton    *cube;

    /* The basic idea of the code is that we do changes as a transaction.
     * If the user changes some widget, we update "pending" to be the
     * settings corresponding to the new widget state, and then start
     * the process of trying to apply the new settings. If applying
     * succeeds, we "commit" the changes by setting "current" to
     * "pending" and saving the changes to GConf. If applying fails
     * we "roll back" by settings"pending" back to "current" and
     * restore the widgets to their old state.
     *
     * In practice, we only roughly correspond to the model; partly
     * for simplicity, and partly to deal with specific complications
     * like the possibility of the roll-back procedure failing.
     */
    Settings		current;
    Settings		pending;
    
    GConfClient	       *gconf;
};

static void set_widgets (App            *app,
                         const Settings *settings);
static gboolean update_window_manager (App     *app,
                                       int      roll_back_count,
                                       GError **err);

static void
update_sensitive (App *app)
{
    gboolean sensitive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (app->compiz));
    
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
    else if (str && strcmp (str, "gnome-shell") == 0)
    {
        return GNOME_SHELL;
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

    return TRUE;
}

static gboolean
start_metacity (App *app, GError **err)
{
    if (!g_spawn_command_line_async ("metacity --replace", err))
	return FALSE;

    return TRUE;
}

static gboolean
start_gnome_shell (App *app, GError **err)
{
    if (!g_spawn_command_line_async ("gnome-shell --replace", err))
	return FALSE;

    return TRUE;
}

static gboolean
start_gnome_panel (App *app, GError **err)
{
    if (!g_spawn_command_line_async ("gnome-panel", err))
	return FALSE;

    return TRUE;
}

static void
get_widget_settings (App *app,
		     Settings *settings)
{
    if (!settings)
	return;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (app->standard)))
        settings->window_manager = METACITY;
    else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (app->compiz)))
        settings->window_manager = COMPIZ;
    else
        settings->window_manager = GNOME_SHELL;
    settings->cube = gtk_toggle_button_get_active (app->cube);
    settings->wobbly = gtk_toggle_button_get_active (app->wobbly);
}

static void
commit_window_manager_change (App *app)
{
    const char *str = NULL;
    char *session_file;

    app->current = app->pending;

    switch (app->current.window_manager)
    {
    case METACITY:
        str = "metacity";
        break;
    case COMPIZ:
        str = "compiz-gtk";
        break;
    case GNOME_SHELL:
        str = "gnome-shell";
        break;
    case UNKNOWN:
        g_assert_not_reached ();
        break;
    }
    g_assert (str != NULL);

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

static WindowManager
get_current_window_manager (void)
{
    Atom utf8_string, atom, type;
    int result;
    WindowManager retval;
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
	return UNKNOWN;
    
    if (type != utf8_string ||
	format !=8 ||
	nitems == 0)
    {
	if (val)
	    XFree (val);
	return UNKNOWN;
    }
    
    /* Retrieved property is always NULL terminated by Xlib */
    if (strcmp ((char *)val, "Metacity") == 0)
        retval = METACITY;
    else if (strcmp ((char *)val, "compiz") == 0)
        retval = COMPIZ;
    else if (strcmp ((char *)val, "Mutter") == 0)
        retval = GNOME_SHELL;
    else
        retval = UNKNOWN;

    XFree (val);
    
    return retval;
}

typedef struct StartWmInfo
{
    App *app;
    GTimer *timer;
    Settings settings;
    int roll_back_count;
} StartWmInfo;

#define SECONDS_WE_WILL_WAIT_FOR_WM_TO_START 8

/* When a window manager fails to start at all, we show
 * a dialog, but maybe the user can't see it */
#define DIALOG_TIMEOUT_MILLISECONDS (10 * 1000)

static void
roll_back_change (App *app)
{
    app->pending = app->current;
    set_widgets (app, &app->current);
}

/* next_wm is usually the old window manager, but could be something
 * else for an emergency fallback */
static void
roll_back_to_window_manager (StartWmInfo  *info,
                             WindowManager next_wm)
{
    WindowManager last_window_manager = info->app->pending.window_manager;

    /* This just changes the widgets and settings, it assumes nothing
     * has actually happened. Since we've already started the new
     * window manager and it's (probably) replaced the old one, we
     * need to restart the old one in addition.
     */
    info->app->current.window_manager = next_wm;
    roll_back_change (info->app);

    update_window_manager (info->app, info->roll_back_count + 1, NULL);

    /* Preserve this so we know where we've already been */
    info->app->current.window_manager = last_window_manager;
}

static gboolean
time_out_dialog (gpointer data)
{
    GtkDialog *dialog = data;
    gtk_dialog_response (dialog, GTK_RESPONSE_OK);

    /* We'll remove the timeout unconditionally afterwards */
    return TRUE;
}

static gboolean
start_wm_timeout (gpointer data)
{
    StartWmInfo *info = data;
    gboolean started;
    
    gtk_window_present (GTK_WINDOW (info->app->dialog));

    started = get_current_window_manager() == info->app->pending.window_manager;

    if (!started &&
        g_timer_elapsed (info->timer, NULL) <= SECONDS_WE_WILL_WAIT_FOR_WM_TO_START)
        return TRUE;

    if (started)
    {
        /* Now that the old environment (which might own the panel D-Bus name)
         * has exited, if we are running a new environment that need gnome-panel
         * start it. If the panel is already running, this will just print a
         * harmless message to standard out so, we don't try to guess if we
         * need it or not ... better to be safe than sorry.
         */
        if (info->app->pending.window_manager != GNOME_SHELL)
            start_gnome_panel (info->app, NULL);

        set_busy (info->app->dialog, FALSE);

        /* We've now gotten to the point where the window manager thinks it's
         * running. If we are going to a new compositor, we show a countdown-dialog
         * that the user has to click on to deal with the case where the
         * compositor started but the display is messed up.
         *
         * If we are starting Metacity, or rolling back to the previous compositor
         * we assume that when we've gotten to this point, we're OK. This isn't
         * /always/ true, especially in the second case, but the confusion of
         * asking the user to confirm that rolling back worked is considerable
         * and not worth the extra safety.
         */
        if (info->app->pending.window_manager == METACITY ||
            info->roll_back_count > 0 ||
            run_timed_dialog (info->app))
            commit_window_manager_change (info->app);
        else {
            roll_back_to_window_manager (info, info->app->current.window_manager);
        }
    }
    else
    {
        GtkWidget *dialog;
        const char *message = NULL;
        GtkMessageType message_type = GTK_MESSAGE_WARNING;
        WindowManager next_wm = info->app->current.window_manager;
        guint dialog_timeout_id;

        set_busy (info->app->dialog, FALSE);

        if (info->roll_back_count > 1 ||
            (info->roll_back_count == 1 &&
             (info->app->current.window_manager == METACITY ||
              info->app->pending.window_manager == METACITY)))
        {
            message = _("Could not restore old settings. Giving up.");
            message_type = GTK_MESSAGE_ERROR;
            next_wm = UNKNOWN; /* We're lost... */
        }
        else
        {
            if (info->roll_back_count == 1)
            {
                message = _("Could not restore old settings. Switching to standard GNOME Desktop.");
                message_type = GTK_MESSAGE_ERROR;
                next_wm = METACITY;
            }
            else
            {
                switch (info->app->pending.window_manager) {
                case METACITY:
                    message = _("Failed to start Metacity. Reverting to previous settings.");
                    break;
                case COMPIZ:
                    message = _("Failed to start Compiz. Reverting to previous settings.");
                    break;
                case GNOME_SHELL:
                    message = _("Failed to start GNOME Shell. Reverting to previous settings.");
                    break;
                case UNKNOWN:
                    g_assert_not_reached ();
                    break;
                }
            }
        }
	
        dialog = gtk_message_dialog_new (
            (GtkWindow *)info->app->dialog,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            message_type,
            GTK_BUTTONS_OK, message);

        gtk_window_set_title (GTK_WINDOW (dialog), "");
        dialog_timeout_id = g_timeout_add (DIALOG_TIMEOUT_MILLISECONDS, time_out_dialog, dialog);
        gtk_dialog_run (GTK_DIALOG (dialog));
        g_source_remove (dialog_timeout_id);
        gtk_widget_destroy (dialog);

        if (next_wm == UNKNOWN) {
            /* Give the user a panel, if possible. As above, if the panel is already
             * running, this is harmless */
            start_gnome_panel (info->app, NULL);
            commit_window_manager_change (info->app);
        } else {
            roll_back_to_window_manager (info, next_wm);
        }
    }
	
    gtk_widget_set_sensitive (info->app->dialog, TRUE);
	
    g_timer_destroy (info->timer);
    g_free (info);
	
    return FALSE;
}

static gboolean
update_window_manager (App     *app,
                       gboolean roll_back_count,
                       GError **err)
{
    StartWmInfo *info;

    if (app->pending.window_manager == COMPIZ)
    {
        if (!start_compiz (app, err))
            return FALSE;
    }
    else if (app->pending.window_manager == GNOME_SHELL)
    {
        if (!start_gnome_shell (app, err))
            return FALSE;
    }
    else
    {
        if (!start_metacity (app, err))
            return FALSE;
    }

    info = g_new0 (StartWmInfo, 1);

    info->app = app;
    info->timer = g_timer_new ();
    info->roll_back_count = roll_back_count;

    set_busy (app->dialog, TRUE);
    gtk_widget_set_sensitive (app->dialog, FALSE);

    g_timeout_add (250, start_wm_timeout, info);

    return TRUE;
}

static void
on_window_manager_toggled (GtkWidget *widget,
                           App       *app)
{
    Settings settings;

    update_sensitive (app);

    get_widget_settings (app, &settings);

    if (app->pending.window_manager != settings.window_manager)
    {
        GError *err = NULL;

        app->pending = settings;

        if (!update_window_manager (app, 0, &err))
        {
	    show_error (err);
            roll_back_change (app);
        }
    }
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
    GSList *new_setting = NULL;
    GSList *old;
    
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
	if (app->pending.wobbly)
	  new_setting = g_slist_prepend (new_setting, "wobbly");
      }
      else if (strcmp (name, "minimize") == 0)
      {
	if (app->pending.cube)
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

    app->current = app->pending;

    return TRUE;
}

static void
on_option_toggled (GtkWidget *widget,
		   App       *app)
{
    Settings settings;

    get_widget_settings (app, &settings);

    if (app->pending.cube != settings.cube ||
        app->pending.wobbly != settings.wobbly)
    {
        app->pending = settings;

        if (!update_plugins (app, NULL))
            roll_back_change (app);
    }
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
    
    result.window_manager = wm;
    result.cube = contains_string (plugins, "cube");
    result.wobbly = contains_string (plugins, "wobbly");
    
    *settings = result;
    
    return TRUE;
}

static void
set_widgets (App            *app,
	     const Settings *settings)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (app->standard),
                                  settings->window_manager == METACITY);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (app->compiz),
                                  settings->window_manager == COMPIZ);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (app->gnome_shell),
                                  settings->window_manager == GNOME_SHELL);
    gtk_toggle_button_set_active (app->cube, settings->cube);
    gtk_toggle_button_set_active (app->wobbly, settings->wobbly);
}

static gboolean
is_in_path(const char *executable)
{
    char *location = g_find_program_in_path (executable);
    gboolean in_path = location != NULL;
    g_free (location);

    return in_path;
}

static gboolean
compiz_installed (void)
{
    return is_in_path ("compiz-gtk");
}

static gboolean
gnome_shell_installed (void)
{
    return is_in_path ("gnome-shell");
}

static gboolean
init_app (App *app,
	  GError **err)
{
#define GLADE_FILE DATADIR "/desktop-effects.glade"
    GladeXML *xml;
    
    app->gconf = gconf_client_get_default ();
    
    xml = glade_xml_new (GLADE_FILE, NULL, NULL);
    
    if (!xml)
    {
	g_warning ("Could not open " GLADE_FILE);
	return FALSE;
    }
    
    app->dialog = glade_xml_get_widget (xml, "dialog");
    app->standard = GTK_RADIO_BUTTON (
	glade_xml_get_widget (xml, "standard_radiobutton"));
    app->compiz = GTK_RADIO_BUTTON (
	glade_xml_get_widget (xml, "compiz_radiobutton"));
    app->gnome_shell = GTK_RADIO_BUTTON (
	glade_xml_get_widget (xml, "gnome_shell_radiobutton"));
    app->cube   = GTK_TOGGLE_BUTTON (
	glade_xml_get_widget (xml, "cube_checkbox"));
    app->wobbly = GTK_TOGGLE_BUTTON (
	glade_xml_get_widget (xml, "wobble_checkbox"));

    if (!compiz_installed ())
        gtk_widget_hide (glade_xml_get_widget (xml, "compiz_box"));

    if (!gnome_shell_installed ())
        gtk_widget_hide (glade_xml_get_widget (xml, "gnome_shell_box"));
    
    g_signal_connect (app->standard, "toggled",
		      G_CALLBACK (on_window_manager_toggled), app);
    g_signal_connect (app->compiz, "toggled",
		      G_CALLBACK (on_window_manager_toggled), app);
    g_signal_connect (app->gnome_shell, "toggled",
		      G_CALLBACK (on_window_manager_toggled), app);
    g_signal_connect (app->wobbly, "toggled",
		      G_CALLBACK (on_option_toggled), app);
    g_signal_connect (app->cube, "toggled",
		      G_CALLBACK (on_option_toggled), app);
    
    /* We assume here that at startup that the GConf settings are accurate
     * and the user hasn't switched window managers by some other means.
     */
    if (!get_gconf_settings (app, &(app->current), err))
	return FALSE;

    app->pending = app->current;
    
    set_widgets (app, &(app->current));
    
    update_sensitive (app);
    
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

static gboolean
has_hardware_gl (void)
{
  GdkScreen *screen = gdk_screen_get_default();
  Display *xdisplay = GDK_SCREEN_XDISPLAY (screen);
  int xscreen = GDK_SCREEN_XNUMBER (screen);
  char *renderer;
  GLXContext context;
  XVisualInfo *visual;
  Window window = None;
  XSetWindowAttributes cwa = { 0 };
  gboolean success = FALSE;

  int attrlist[] = {
    GLX_RGBA,
    GLX_RED_SIZE, 1,
    GLX_GREEN_SIZE, 1,
    GLX_BLUE_SIZE, 1,
    GLX_DOUBLEBUFFER,
    None
  };

  screen = gdk_screen_get_default ();
  xdisplay = GDK_SCREEN_XDISPLAY (screen);
  xscreen = GDK_SCREEN_XNUMBER (screen);

  visual = glXChooseVisual (xdisplay, xscreen, attrlist);
  if (!visual)
      goto out;

  context = glXCreateContext (xdisplay, visual, NULL, True);
  if (!context)
      goto out;

  cwa.colormap = XCreateColormap(xdisplay, RootWindow (xdisplay, xscreen), visual->visual, False);
  window = XCreateWindow(xdisplay,
			 RootWindow (xdisplay, xscreen),
			 0, 0, 1, 1, 0,
			 visual->depth, InputOutput, visual->visual,
			 CWColormap,
			 &cwa);

  if (!glXMakeCurrent(xdisplay, window, context))
      goto out;

  renderer = g_ascii_strdown ((const char *)glGetString(GL_RENDERER), -1);
  /* The current Mesa software GL renderer string is "Software Rasterizer" */
  success = strstr (renderer, "software rasterizer") == NULL;
  g_free (renderer);

 out:
  glXMakeCurrent (xdisplay, None, None);
  if (context)
    glXDestroyContext (xdisplay, context);
  if (window)
    XDestroyWindow (xdisplay, window);
  if (cwa.colormap)
    XFreeColormap (xdisplay, cwa.colormap);

  return success;
}

static void
show_alert (const char *text,
            const char *secondary_text)
{
    GtkWidget *dialog;
    
    dialog = gtk_message_dialog_new_with_markup (NULL, GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 "<span weight='bold' size='larger'>%s</span>",
                                                 text);
    if (secondary_text)
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                  "%s",
                                                  secondary_text);
    
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
        /* Intentionally not marked for translation, since it's exceedingly
         * unlikely */
	show_alert ("The Composite extension is not available", NULL);
	return 0;
    }

    if (!has_hardware_gl())
    {
	show_alert (_("Accelerated 3D graphics is not available"),
                    _("Desktop effects require hardware 3D support."));
	return 0;
    }

    if (!compiz_installed () && !gnome_shell_installed ())
    {
        /* compiz-gnome and gnome-shell are package names and should not be translated */
	show_alert (_("Only the standard GNOME desktop is available"),
                    _("Please install compiz-gnome or gnome-shell."));
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
