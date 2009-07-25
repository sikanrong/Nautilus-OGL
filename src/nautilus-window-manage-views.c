/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *
 *  Nautilus is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  Nautilus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>
 *           John Sullivan <sullivan@eazel.com>
 *           Darin Adler <darin@bentspoon.com>
 */

#include <config.h>
#include "nautilus-window-manage-views.h"

#include "nautilus-actions.h"
#include "nautilus-application.h"
#include "nautilus-location-bar.h"
#include "nautilus-search-bar.h"
#include "nautilus-pathbar.h"
#include "nautilus-main.h"
#include "nautilus-window-private.h"
#include "nautilus-window-slot.h"
#include "nautilus-navigation-window-slot.h"
#include "nautilus-trash-bar.h"
#include "nautilus-x-content-bar.h"
#include "nautilus-zoom-control.h"
#include <eel/eel-accessibility.h>
#include <eel/eel-debug.h>
#include <eel/eel-gdk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <libnautilus-extension/nautilus-location-widget-provider.h>
#include <libnautilus-private/nautilus-debug-log.h>
#include <libnautilus-private/nautilus-file-attributes.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-file.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-metadata.h>
#include <libnautilus-private/nautilus-mime-actions.h>
#include <libnautilus-private/nautilus-module.h>
#include <libnautilus-private/nautilus-monitor.h>
#include <libnautilus-private/nautilus-search-directory.h>
#include <libnautilus-private/nautilus-view-factory.h>
#include <libnautilus-private/nautilus-window-info.h>
#include <libnautilus-private/nautilus-window-slot-info.h>
#include <libnautilus-private/nautilus-autorun.h>

/* FIXME bugzilla.gnome.org 41243: 
 * We should use inheritance instead of these special cases
 * for the desktop window.
 */
#include "nautilus-desktop-window.h"

/* This number controls a maximum character count for a URL that is
 * displayed as part of a dialog. It's fairly arbitrary -- big enough
 * to allow most "normal" URIs to display in full, but small enough to
 * prevent the dialog from getting insanely wide.
 */
#define MAX_URI_IN_DIALOG_LENGTH 60

static void begin_location_change                     (NautilusWindowSlot         *slot,
						       GFile                      *location,
						       GList                      *new_selection,
						       NautilusLocationChangeType  type,
						       guint                       distance,
						       const char                 *scroll_pos);
static void free_location_change                      (NautilusWindowSlot         *slot);
static void end_location_change                       (NautilusWindowSlot         *slot);
static void cancel_location_change                    (NautilusWindowSlot         *slot);
static void got_file_info_for_view_selection_callback (NautilusFile               *file,
						       gpointer                    callback_data);
static void create_content_view                       (NautilusWindowSlot         *slot,
						       const char                 *view_id);
static void display_view_selection_failure            (NautilusWindow             *window,
						       NautilusFile               *file,
						       GFile                      *location,
						       GError                     *error);
static void load_new_location                         (NautilusWindowSlot         *slot,
						       GFile                      *location,
						       GList                      *selection,
						       gboolean                    tell_current_content_view,
						       gboolean                    tell_new_content_view);
static void location_has_really_changed               (NautilusWindowSlot         *slot);
static void update_for_new_location                   (NautilusWindowSlot         *slot);

void
nautilus_window_report_selection_changed (NautilusWindowInfo *window)
{
	if (window->details->temporarily_ignore_view_signals) {
		return;
	}

	g_signal_emit_by_name (window, "selection_changed");
}

/* set_displayed_location:
 */
static void
set_displayed_location (NautilusWindowSlot *slot, GFile *location)
{
	NautilusWindow *window;
        GFile *bookmark_location;
        gboolean recreate;
	char *name;

	window = slot->window;
        
        if (slot->current_location_bookmark == NULL || location == NULL) {
                recreate = TRUE;
        } else {
                bookmark_location = nautilus_bookmark_get_location (slot->current_location_bookmark);
                recreate = !g_file_equal (bookmark_location, location);
                g_object_unref (bookmark_location);
        }
        
        if (recreate) {
                /* We've changed locations, must recreate bookmark for current location. */
		if (slot->last_location_bookmark != NULL)  {
			g_object_unref (slot->last_location_bookmark);
                }
		slot->last_location_bookmark = slot->current_location_bookmark;
		name = g_file_get_uri (location);
		slot->current_location_bookmark = (location == NULL) ? NULL
                        : nautilus_bookmark_new (location, name);
		g_free (name);
        }

	nautilus_window_slot_update_title (slot);
	nautilus_window_slot_update_icon (slot);
}

static void
check_bookmark_location_matches (NautilusBookmark *bookmark, GFile *location)
{
        GFile *bookmark_location;
        char *bookmark_uri, *uri;

	bookmark_location = nautilus_bookmark_get_location (bookmark);
	if (!g_file_equal (location, bookmark_location)) {
		bookmark_uri = g_file_get_uri (bookmark_location);
		uri = g_file_get_uri (location);
		g_warning ("bookmark uri is %s, but expected %s", bookmark_uri, uri);
		g_free (uri);
		g_free (bookmark_uri);
	}
	g_object_unref (bookmark_location);
}

/* Debugging function used to verify that the last_location_bookmark
 * is in the state we expect when we're about to use it to update the
 * Back or Forward list.
 */
static void
check_last_bookmark_location_matches_slot (NautilusWindowSlot *slot)
{
	check_bookmark_location_matches (slot->last_location_bookmark,
					 slot->location);
}

static void
handle_go_back (NautilusNavigationWindowSlot *navigation_slot,
		GFile *location)
{
	NautilusWindowSlot *slot;
	guint i;
	GList *link;
	NautilusBookmark *bookmark;

	slot = NAUTILUS_WINDOW_SLOT (navigation_slot);

	/* Going back. Move items from the back list to the forward list. */
	g_assert (g_list_length (navigation_slot->back_list) > slot->location_change_distance);
	check_bookmark_location_matches (NAUTILUS_BOOKMARK (g_list_nth_data (navigation_slot->back_list,
									     slot->location_change_distance)),
					 location);
	g_assert (slot->location != NULL);
	
	/* Move current location to Forward list */

	check_last_bookmark_location_matches_slot (slot);

	/* Use the first bookmark in the history list rather than creating a new one. */
	navigation_slot->forward_list = g_list_prepend (navigation_slot->forward_list,
							     slot->last_location_bookmark);
	g_object_ref (navigation_slot->forward_list->data);
				
	/* Move extra links from Back to Forward list */
	for (i = 0; i < slot->location_change_distance; ++i) {
		bookmark = NAUTILUS_BOOKMARK (navigation_slot->back_list->data);
		navigation_slot->back_list =
			g_list_remove (navigation_slot->back_list, bookmark);
		navigation_slot->forward_list =
			g_list_prepend (navigation_slot->forward_list, bookmark);
	}
	
	/* One bookmark falls out of back/forward lists and becomes viewed location */
	link = navigation_slot->back_list;
	navigation_slot->back_list = g_list_remove_link (navigation_slot->back_list, link);
	g_object_unref (link->data);
	g_list_free_1 (link);
}

static void
handle_go_forward (NautilusNavigationWindowSlot *navigation_slot,
		   GFile *location)
{
	NautilusWindowSlot *slot;
	guint i;
	GList *link;
	NautilusBookmark *bookmark;

	slot = NAUTILUS_WINDOW_SLOT (navigation_slot);

	/* Going forward. Move items from the forward list to the back list. */
	g_assert (g_list_length (navigation_slot->forward_list) > slot->location_change_distance);
	check_bookmark_location_matches (NAUTILUS_BOOKMARK (g_list_nth_data (navigation_slot->forward_list,
									     slot->location_change_distance)),
					 location);
	g_assert (slot->location != NULL);
				
	/* Move current location to Back list */
	check_last_bookmark_location_matches_slot (slot);
	
	/* Use the first bookmark in the history list rather than creating a new one. */
      navigation_slot->back_list = g_list_prepend (navigation_slot->back_list,
							  slot->last_location_bookmark);
      g_object_ref (navigation_slot->back_list->data);
	
	/* Move extra links from Forward to Back list */
      for (i = 0; i < slot->location_change_distance; ++i) {
		bookmark = NAUTILUS_BOOKMARK (navigation_slot->forward_list->data);
		navigation_slot->forward_list =
			g_list_remove (navigation_slot->back_list, bookmark);
		navigation_slot->back_list =
			g_list_prepend (navigation_slot->forward_list, bookmark);
	}
	
	/* One bookmark falls out of back/forward lists and becomes viewed location */
	link = navigation_slot->forward_list;
	navigation_slot->forward_list = g_list_remove_link (navigation_slot->forward_list, link);
	g_object_unref (link->data);
	g_list_free_1 (link);
}

static void
handle_go_elsewhere (NautilusWindowSlot *slot, GFile *location)
{
#if !NEW_UI_COMPLETE
	NautilusNavigationWindowSlot *navigation_slot;

	if (NAUTILUS_IS_NAVIGATION_WINDOW_SLOT (slot)) {
		navigation_slot = NAUTILUS_NAVIGATION_WINDOW_SLOT (slot);

		/* Clobber the entire forward list, and move displayed location to back list */
		nautilus_navigation_window_slot_clear_forward_list (navigation_slot);
		
		if (slot->location != NULL) {
			/* If we're returning to the same uri somehow, don't put this uri on back list. 
			 * This also avoids a problem where set_displayed_location
			 * didn't update last_location_bookmark since the uri didn't change.
			 */
			if (!g_file_equal (slot->location, location)) {
				/* Store bookmark for current location in back list, unless there is no current location */
				check_last_bookmark_location_matches_slot (slot);
				/* Use the first bookmark in the history list rather than creating a new one. */
				navigation_slot->back_list = g_list_prepend (navigation_slot->back_list,
									     slot->last_location_bookmark);
				g_object_ref (navigation_slot->back_list->data);
			}
		}
	}
#endif
}

static void
update_up_button (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
        gboolean allowed;
	GFile *parent;

	slot = window->details->active_slot;

        allowed = FALSE;
        if (slot->location != NULL) {
		parent = g_file_get_parent (slot->location);
		allowed = parent != NULL;
		if (parent != NULL) {
			g_object_unref (parent);
		}
        }

        nautilus_window_allow_up (window, allowed);
}

static void
viewed_file_changed_callback (NautilusFile *file,
                              NautilusWindowSlot *slot)
{
	NautilusWindow *window;
        GFile *new_location;
	gboolean is_in_trash, was_in_trash;

	window = slot->window;

        g_assert (NAUTILUS_IS_FILE (file));
	g_assert (NAUTILUS_IS_WINDOW (window));

	g_assert (file == slot->viewed_file);

        if (!nautilus_file_is_not_yet_confirmed (file)) {
                slot->viewed_file_seen = TRUE;
        }

	was_in_trash = slot->viewed_file_in_trash;

	slot->viewed_file_in_trash = is_in_trash = nautilus_file_is_in_trash (file);

	/* Close window if the file it's viewing has been deleted or moved to trash. */
	if (nautilus_file_is_gone (file) || (is_in_trash && !was_in_trash)) {
                /* Don't close the window in the case where the
                 * file was never seen in the first place.
                 */
                if (slot->viewed_file_seen) {
                        /* Detecting a file is gone may happen in the
                         * middle of a pending location change, we
                         * need to cancel it before closing the window
                         * or things break.
                         */
                        /* FIXME: It makes no sense that this call is
                         * needed. When the window is destroyed, it
                         * calls nautilus_window_manage_views_destroy,
                         * which calls free_location_change, which
                         * should be sufficient. Also, if this was
                         * really needed, wouldn't it be needed for
                         * all other nautilus_window_close callers?
                         */
			end_location_change (slot);

			if (NAUTILUS_IS_NAVIGATION_WINDOW (window)) {
				/* auto-show existing parent. */
				GFile *go_to_file, *parent, *location;

				go_to_file = NULL;
				location =  nautilus_file_get_location (file);
				parent = g_file_get_parent (location);
				g_object_unref (location);
				if (parent) {
					go_to_file = nautilus_find_existing_uri_in_hierarchy (parent);
					g_object_unref (parent);
				}
				
				if (go_to_file != NULL) {
					/* the path bar URI will be set to go_to_uri immediately
					 * in begin_location_change, but we don't want the
					 * inexistant children to show up anymore */
					if (slot == window->details->active_slot) {
						/* multiview-TODO also update NautilusWindowSlot
						 * [which as of writing doesn't save/store any path bar state]
						 */
						nautilus_path_bar_clear_buttons (NAUTILUS_PATH_BAR (NAUTILUS_NAVIGATION_WINDOW (window)->path_bar));
					}

					nautilus_window_slot_go_to (slot, go_to_file, FALSE);
					g_object_unref (go_to_file);
				} else {
					nautilus_window_slot_go_home (slot, FALSE);
				}
			} else {
				nautilus_window_close (window);
			}
                }
	} else {
                new_location = nautilus_file_get_location (file);

                /* If the file was renamed, update location and/or
                 * title. */
                if (!g_file_equal (new_location,
				   slot->location)) {
                        g_object_unref (slot->location);
                        slot->location = new_location;
			if (slot == window->details->active_slot) {
				nautilus_window_sync_location_widgets (window);
			}
                } else {
			/* TODO?
 			 *   why do we update title & icon at all in this case? */
                        g_object_unref (new_location);
                }

                nautilus_window_slot_update_title (slot);
		nautilus_window_slot_update_icon (slot);
        }
}

static void
update_history (NautilusWindowSlot *slot,
                NautilusLocationChangeType type,
                GFile *new_location)
{
        switch (type) {
        case NAUTILUS_LOCATION_CHANGE_STANDARD:
        case NAUTILUS_LOCATION_CHANGE_FALLBACK:
                nautilus_window_slot_add_current_location_to_history_list (slot);
                handle_go_elsewhere (slot, new_location);
                return;
        case NAUTILUS_LOCATION_CHANGE_RELOAD:
                /* for reload there is no work to do */
                return;
        case NAUTILUS_LOCATION_CHANGE_BACK:
                nautilus_window_slot_add_current_location_to_history_list (slot);
                handle_go_back (NAUTILUS_NAVIGATION_WINDOW_SLOT (slot), new_location);
                return;
        case NAUTILUS_LOCATION_CHANGE_FORWARD:
                nautilus_window_slot_add_current_location_to_history_list (slot);
                handle_go_forward (NAUTILUS_NAVIGATION_WINDOW_SLOT (slot), new_location);
                return;
        case NAUTILUS_LOCATION_CHANGE_REDIRECT:
                /* for the redirect case, the caller can do the updating */
                return;
        }
	g_return_if_fail (FALSE);
}

static void
cancel_viewed_file_changed_callback (NautilusWindowSlot *slot)
{
        NautilusFile *file;

        file = slot->viewed_file;
        if (file != NULL) {
                g_signal_handlers_disconnect_by_func (G_OBJECT (file),
                                                      G_CALLBACK (viewed_file_changed_callback),
						      slot);
                nautilus_file_monitor_remove (file, &slot->viewed_file);
        }
}

static void
new_window_show_callback (GtkWidget *widget,
                          gpointer user_data)
{
        NautilusWindow *window;
        
        window = NAUTILUS_WINDOW (user_data);
        
        nautilus_window_close (window);

        g_signal_handlers_disconnect_by_func (widget, 
                                              G_CALLBACK (new_window_show_callback),
                                              user_data);
}


void
nautilus_window_slot_open_location_full (NautilusWindowSlot *slot,
					 GFile *location,
					 NautilusWindowOpenMode mode,
					 NautilusWindowOpenFlags flags,
					 GList *new_selection)
{
	NautilusWindow *window;
        NautilusWindow *target_window;
        NautilusWindowSlot *target_slot;
	NautilusWindowOpenFlags slot_flags;
        gboolean do_load_location = TRUE;
	GFile *old_location;
	char *old_uri, *new_uri;
	int new_slot_position;

	window = slot->window;

        target_window = NULL;
	target_slot = NULL;

	old_uri = nautilus_window_slot_get_location_uri (slot);
	if (old_uri == NULL) {
		old_uri = g_strdup ("(none)");
	}
	new_uri = g_file_get_uri (location);
	nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
			    "window %p open location: old=\"%s\", new=\"%s\"",
			    window,
			    old_uri,
			    new_uri);
	g_free (old_uri);
	g_free (new_uri);

	g_assert (!((flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW) != 0 &&
		    (flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB) != 0));


	old_location = nautilus_window_slot_get_location (slot);
	switch (mode) {
        case NAUTILUS_WINDOW_OPEN_ACCORDING_TO_MODE :
		if (eel_preferences_get_boolean (NAUTILUS_PREFERENCES_ALWAYS_USE_BROWSER)) {
			target_window = window;
			if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
				if (!NAUTILUS_SPATIAL_WINDOW (window)->affect_spatial_window_on_next_location_change) {
					target_window = nautilus_application_create_navigation_window 
						(window->application,
						 NULL,
						 gtk_window_get_screen (GTK_WINDOW (window)));
				} else {
					NAUTILUS_SPATIAL_WINDOW (window)->affect_spatial_window_on_next_location_change = FALSE;
				}
			} else if ((flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW) != 0) {
				target_window = nautilus_application_create_navigation_window 
					(window->application,
					 NULL,
					 gtk_window_get_screen (GTK_WINDOW (window)));
			}
		} else if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
                        if (!NAUTILUS_SPATIAL_WINDOW (window)->affect_spatial_window_on_next_location_change) {
                                target_window = nautilus_application_present_spatial_window_with_selection (
                                        window->application,
					window,
					NULL,
                                        location,
					new_selection,
                                        gtk_window_get_screen (GTK_WINDOW (window)));
                                do_load_location = FALSE;
                        } else {
                                NAUTILUS_SPATIAL_WINDOW (window)->affect_spatial_window_on_next_location_change = FALSE;
                                target_window = window;
                        }
		} else if (flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW) {
			target_window = nautilus_application_create_navigation_window 
				(window->application,
				 NULL,
				 gtk_window_get_screen (GTK_WINDOW (window)));
		} else {
                        target_window = window;
                }       
                break;
        case NAUTILUS_WINDOW_OPEN_IN_SPATIAL :
                target_window = nautilus_application_present_spatial_window (
                        window->application,
			window,
			NULL,
                        location,
                        gtk_window_get_screen (GTK_WINDOW (window)));
                break;
        case NAUTILUS_WINDOW_OPEN_IN_NAVIGATION :
                target_window = nautilus_application_create_navigation_window 
                        (window->application,
			 NULL,
                         gtk_window_get_screen (GTK_WINDOW (window)));
                break;
        default :
                g_warning ("Unknown open location mode");
		g_object_unref (old_location);
                return;
        }

        g_assert (target_window != NULL);

	if ((flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB) != 0 &&
	    NAUTILUS_IS_NAVIGATION_WINDOW (window)) {
		g_assert (target_window == window);

		slot_flags = 0;

		new_slot_position = eel_preferences_get_enum (NAUTILUS_PREFERENCES_NEW_TAB_POSITION);
		if (new_slot_position == NAUTILUS_NEW_TAB_POSITION_END) {
			slot_flags = NAUTILUS_WINDOW_OPEN_SLOT_APPEND;
		}

		target_slot = nautilus_window_open_slot (window, slot_flags);
	}

        if ((flags & NAUTILUS_WINDOW_OPEN_FLAG_CLOSE_BEHIND) != 0) {
                if (NAUTILUS_IS_SPATIAL_WINDOW (window) && !NAUTILUS_IS_DESKTOP_WINDOW (window)) {
                        if (GTK_WIDGET_VISIBLE (target_window)) {
                                nautilus_window_close (window);
                        } else {
                                g_signal_connect_object (target_window,
                                                         "show",
                                                         G_CALLBACK (new_window_show_callback),
                                                         window,
                                                         G_CONNECT_AFTER);
                        }
                }
        }

	if (target_slot == NULL) {
		if (target_window == window) {
			target_slot = slot;
		} else {
			target_slot = target_window->details->active_slot;
		}
	}

        if ((!do_load_location) ||
	    (target_window == window && target_slot == slot &&
	     old_location && g_file_equal (old_location, location))) {
		if (old_location) {
			g_object_unref (old_location);
		}
                return;
        }
	
	if (old_location) {
		g_object_unref (old_location);
	}

        begin_location_change (target_slot, location, new_selection,
                               NAUTILUS_LOCATION_CHANGE_STANDARD, 0, NULL);
}

void
nautilus_window_slot_open_location (NautilusWindowSlot *slot,
				    GFile *location,
				    gboolean close_behind)
{
	NautilusWindowOpenFlags flags;

	flags = 0;
	if (close_behind) {
		flags = NAUTILUS_WINDOW_OPEN_FLAG_CLOSE_BEHIND;
	}
	
	nautilus_window_slot_open_location_full (slot, location,
						 NAUTILUS_WINDOW_OPEN_ACCORDING_TO_MODE,
						 flags, NULL);
}

void
nautilus_window_slot_open_location_with_selection (NautilusWindowSlot *slot,
						   GFile *location,
						   GList *selection,
						   gboolean close_behind)
{
	NautilusWindowOpenFlags flags;

	flags = 0;
	if (close_behind) {
		flags = NAUTILUS_WINDOW_OPEN_FLAG_CLOSE_BEHIND;
	}
	nautilus_window_slot_open_location_full (slot, location,
						 NAUTILUS_WINDOW_OPEN_ACCORDING_TO_MODE,
						 flags, selection);
}


void
nautilus_window_slot_go_home (NautilusWindowSlot *slot, gboolean new_tab)
{			      
	GFile *home;
	NautilusWindowOpenFlags flags;

	g_return_if_fail (NAUTILUS_IS_WINDOW_SLOT (slot));

	if (new_tab) {
		flags = NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB;
	} else {
		flags = 0;
	}

	home = g_file_new_for_path (g_get_home_dir ());
	nautilus_window_slot_open_location_full (slot, home, 
						 NAUTILUS_WINDOW_OPEN_ACCORDING_TO_MODE, 
						 flags, NULL);
	g_object_unref (home);
}

#if 0
static char *
nautilus_window_slot_get_view_label (NautilusWindowSlot *slot)
{
	const NautilusViewInfo *info;

	info = nautilus_view_factory_lookup (nautilus_window_slot_get_content_view_id (slot));

	return g_strdup (info->label);
}
#endif

static char *
nautilus_window_slot_get_view_error_label (NautilusWindowSlot *slot)
{
	const NautilusViewInfo *info;

	info = nautilus_view_factory_lookup (nautilus_window_slot_get_content_view_id (slot));

	return g_strdup (info->error_label);
}

static char *
nautilus_window_slot_get_view_startup_error_label (NautilusWindowSlot *slot)
{
	const NautilusViewInfo *info;

	info = nautilus_view_factory_lookup (nautilus_window_slot_get_content_view_id (slot));

	return g_strdup (info->startup_error_label);
}

static void
report_current_content_view_failure_to_user (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	char *message;

	window = slot->window;

	message = nautilus_window_slot_get_view_startup_error_label (slot);
  	eel_show_error_dialog (message,
  			       _("You can choose another view or go to a different location."),
  			       GTK_WINDOW (window));
	g_free (message);
}

static void
report_nascent_content_view_failure_to_user (NautilusWindowSlot *slot,
					     NautilusView *view)
{
	NautilusWindow *window;
	char *message;

	window = slot->window;

	/* TODO? why are we using the current view's error label here, instead of the next view's?
 	 * This behavior has already been present in pre-slot days.
 	 */
	message = nautilus_window_slot_get_view_error_label (slot);
	eel_show_error_dialog (message,
			       _("The location cannot be displayed with this viewer."),
			       GTK_WINDOW (window));
	g_free (message);
}


const char *
nautilus_window_slot_get_content_view_id (NautilusWindowSlot *slot)
{
	if (slot->content_view == NULL) {
		return NULL;
	}
	return nautilus_view_get_view_id (slot->content_view);
}

gboolean
nautilus_window_slot_content_view_matches_iid (NautilusWindowSlot *slot, 
					       const char *iid)
{
	if (slot->content_view == NULL) {
		return FALSE;
	}
	return eel_strcmp (nautilus_view_get_view_id (slot->content_view), iid) == 0;
}


/*
 * begin_location_change
 * 
 * Change a window's location.
 * @window: The NautilusWindow whose location should be changed.
 * @location: A url specifying the location to load
 * @new_selection: The initial selection to present after loading the location
 * @type: Which type of location change is this? Standard, back, forward, or reload?
 * @distance: If type is back or forward, the index into the back or forward chain. If
 * type is standard or reload, this is ignored, and must be 0.
 * @scroll_pos: The file to scroll to when the location is loaded.
 *
 * This is the core function for changing the location of a window. Every change to the
 * location begins here.
 */
static void
begin_location_change (NautilusWindowSlot *slot,
                       GFile *location,
		       GList *new_selection,
                       NautilusLocationChangeType type,
                       guint distance,
                       const char *scroll_pos)
{
	NautilusWindow *window;
        NautilusDirectory *directory;
        NautilusFile *file;
	gboolean force_reload;
        char *current_pos;

	g_assert (slot != NULL);
        g_assert (location != NULL);
        g_assert (type == NAUTILUS_LOCATION_CHANGE_BACK
                  || type == NAUTILUS_LOCATION_CHANGE_FORWARD
                  || distance == 0);

	window = slot->window;
        g_assert (NAUTILUS_IS_WINDOW (window));
        g_object_ref (window);

	end_location_change (slot);

	nautilus_window_slot_set_allow_stop (slot, TRUE);
	nautilus_window_slot_set_status (slot, " ");

	g_assert (slot->pending_location == NULL);
	g_assert (slot->pending_selection == NULL);
	
	slot->pending_location = g_object_ref (location);
        slot->location_change_type = type;
        slot->location_change_distance = distance;
	slot->tried_mount = FALSE;
	slot->pending_selection = eel_g_object_list_copy (new_selection);

	slot->pending_scroll_to = g_strdup (scroll_pos);
        
        directory = nautilus_directory_get (location);

	/* The code to force a reload is here because if we do it
	 * after determining an initial view (in the components), then
	 * we end up fetching things twice.
	 */
	if (type == NAUTILUS_LOCATION_CHANGE_RELOAD) {
		force_reload = TRUE;
	} else if (!nautilus_monitor_active ()) {
		force_reload = TRUE;
	} else {
		force_reload = !nautilus_directory_is_local (directory);
	}

	if (force_reload) {
		nautilus_directory_force_reload (directory);
		file = nautilus_directory_get_corresponding_file (directory);
		nautilus_file_invalidate_all_attributes (file);
		nautilus_file_unref (file);
	}

        nautilus_directory_unref (directory);

        /* Set current_bookmark scroll pos */
        if (slot->current_location_bookmark != NULL &&
            slot->content_view != NULL) {
                current_pos = nautilus_view_get_first_visible_file (slot->content_view);
                nautilus_bookmark_set_scroll_pos (slot->current_location_bookmark, current_pos);
                g_free (current_pos);
        }

	/* Get the info needed for view selection */
	
        slot->determine_view_file = nautilus_file_get (location);
	g_assert (slot->determine_view_file != NULL);

	/* if the currently viewed file is marked gone while loading the new location,
	 * this ensures that the window isn't destroyed */
        cancel_viewed_file_changed_callback (slot);

	nautilus_file_call_when_ready (slot->determine_view_file,
				       NAUTILUS_FILE_ATTRIBUTE_INFO |
				       NAUTILUS_FILE_ATTRIBUTE_MOUNT,
                                       got_file_info_for_view_selection_callback,
				       slot);

        g_object_unref (window);
}

static void
setup_new_spatial_window (NautilusWindowSlot *slot, NautilusFile *file)
{
	NautilusWindow *window;
	char *show_hidden_file_setting;
	char *geometry_string;
	char *scroll_string;
	gboolean maximized, sticky, above;

	window = slot->window;

	if (NAUTILUS_IS_SPATIAL_WINDOW (window) && !NAUTILUS_IS_DESKTOP_WINDOW (window)) {
		/* load show hidden state */
		show_hidden_file_setting = nautilus_file_get_metadata 
			(file, NAUTILUS_METADATA_KEY_WINDOW_SHOW_HIDDEN_FILES,
			 NULL);
		if (show_hidden_file_setting != NULL) {
			if (strcmp (show_hidden_file_setting, "1") == 0) {
				NAUTILUS_WINDOW (window)->details->show_hidden_files_mode = NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_ENABLE;	
			} else {
				NAUTILUS_WINDOW (window)->details->show_hidden_files_mode = NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_DISABLE;
			}
		} else {
			NAUTILUS_WINDOW (window)->details->show_hidden_files_mode = NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_DEFAULT;
		}
		g_free (show_hidden_file_setting);
		
		/* load the saved window geometry */
		maximized = nautilus_file_get_boolean_metadata
			(file, NAUTILUS_METADATA_KEY_WINDOW_MAXIMIZED, FALSE);
		if (maximized) {
			gtk_window_maximize (GTK_WINDOW (window));
		} else {
			gtk_window_unmaximize (GTK_WINDOW (window));
		}

		sticky = nautilus_file_get_boolean_metadata
			(file, NAUTILUS_METADATA_KEY_WINDOW_STICKY, FALSE);
		if (sticky) {
			gtk_window_stick (GTK_WINDOW (window));
		} else {
			gtk_window_unstick (GTK_WINDOW (window));
		}

		above = nautilus_file_get_boolean_metadata
			(file, NAUTILUS_METADATA_KEY_WINDOW_KEEP_ABOVE, FALSE);
		if (above) {
			gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
		} else {
			gtk_window_set_keep_above (GTK_WINDOW (window), FALSE);
		}

		geometry_string = nautilus_file_get_metadata 
			(file, NAUTILUS_METADATA_KEY_WINDOW_GEOMETRY, NULL);
                if (geometry_string != NULL) {
                        eel_gtk_window_set_initial_geometry_from_string 
                                (GTK_WINDOW (window), 
                                 geometry_string,
                                 NAUTILUS_SPATIAL_WINDOW_MIN_WIDTH, 
                                 NAUTILUS_SPATIAL_WINDOW_MIN_HEIGHT,
				 FALSE);
                }
                g_free (geometry_string);

		if (slot->pending_selection == NULL) {
			/* If there is no pending selection, then load the saved scroll position. */
			scroll_string = nautilus_file_get_metadata 
				(file, NAUTILUS_METADATA_KEY_WINDOW_SCROLL_POSITION,
				 NULL);
		} else {
			/* If there is a pending selection, we want to scroll to an item in
			 * the pending selection list. */
			scroll_string = g_file_get_uri (slot->pending_selection->data);
		}

		/* scroll_string might be NULL if there was no saved scroll position. */
		if (scroll_string != NULL) {
			slot->pending_scroll_to = scroll_string;
		}
        }
}

typedef struct {
	GCancellable *cancellable;
	NautilusWindowSlot *slot;
} MountNotMountedData;

static void 
mount_not_mounted_callback (GObject *source_object,
			    GAsyncResult *res,
			    gpointer user_data)
{
	MountNotMountedData *data;
	NautilusWindow *window;
	NautilusWindowSlot *slot;
	GError *error;
	GCancellable *cancellable;

	data = user_data;
	slot = data->slot;
	window = slot->window;
	cancellable = data->cancellable;
	g_free (data);

	if (g_cancellable_is_cancelled (cancellable)) {
		/* Cancelled, don't call back */
		g_object_unref (cancellable);
		return;
	}

	slot->mount_cancellable = NULL;

	slot->determine_view_file = nautilus_file_get (slot->pending_location);
	
	error = NULL;
	if (!g_file_mount_enclosing_volume_finish (G_FILE (source_object), res, &error)) {
		slot->mount_error = error;
		got_file_info_for_view_selection_callback (slot->determine_view_file, slot);
		slot->mount_error = NULL;
		g_error_free (error);
	} else {
		nautilus_file_invalidate_all_attributes (slot->determine_view_file);
		nautilus_file_call_when_ready (slot->determine_view_file,
					       NAUTILUS_FILE_ATTRIBUTE_INFO,
					       got_file_info_for_view_selection_callback,
					       slot);
	}

	g_object_unref (cancellable);
}

static void
got_file_info_for_view_selection_callback (NautilusFile *file,
					   gpointer callback_data)
{
        GError *error;
	char *view_id;
	char *mimetype;
	NautilusWindow *window;
	NautilusWindowSlot *slot;
	NautilusFile *viewed_file;
	GFile *location;
	GMountOperation *mount_op;
	MountNotMountedData *data;

	slot = callback_data;
	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));
	g_assert (slot->determine_view_file == file);

	window = slot->window;
	g_assert (NAUTILUS_IS_WINDOW (window));

	slot->determine_view_file = NULL;

	if (slot->mount_error) {
		error = slot->mount_error;
	} else {
		error = nautilus_file_get_file_info_error (file);
	}

	if (error && error->domain == G_IO_ERROR && error->code == G_IO_ERROR_NOT_MOUNTED &&
	    !slot->tried_mount) {
		slot->tried_mount = TRUE;

		mount_op = gtk_mount_operation_new (GTK_WINDOW (window));
		location = nautilus_file_get_location (file);
		data = g_new0 (MountNotMountedData, 1);
		data->cancellable = g_cancellable_new ();
		data->slot = slot;
		slot->mount_cancellable = data->cancellable;
		g_file_mount_enclosing_volume (location, 0, mount_op, slot->mount_cancellable,
					       mount_not_mounted_callback, data);
		g_object_unref (location);
		g_object_unref (mount_op);

		nautilus_file_unref (file);

		return;
	}
	
	location = slot->pending_location;
	
	view_id = NULL;
	
        if (error == NULL ||
	    (error->domain == G_IO_ERROR && error->code == G_IO_ERROR_NOT_SUPPORTED)) {
		/* We got the information we need, now pick what view to use: */

		mimetype = nautilus_file_get_mime_type (file);

		/* If fallback, don't use view from metadata */
		if (slot->location_change_type != NAUTILUS_LOCATION_CHANGE_FALLBACK) {
			/* Look in metadata for view */
			view_id = nautilus_file_get_metadata 
				(file, NAUTILUS_METADATA_KEY_DEFAULT_VIEW, NULL);
			if (view_id != NULL && 
			    !nautilus_view_factory_view_supports_uri (view_id,
								      location,
								      nautilus_file_get_file_type (file),
								      mimetype)) {
				g_free (view_id);
				view_id = NULL;
			}
		}

		/* Otherwise, use default */
		if (view_id == NULL) {
			view_id = nautilus_global_preferences_get_default_folder_viewer_preference_as_iid ();

			if (view_id != NULL && 
			    !nautilus_view_factory_view_supports_uri (view_id,
								      location,
								      nautilus_file_get_file_type (file),
								      mimetype)) {
				g_free (view_id);
				view_id = NULL;
			}
		}
		
		g_free (mimetype);
	}

	if (view_id != NULL) {
                if (!GTK_WIDGET_VISIBLE (window) && NAUTILUS_IS_SPATIAL_WINDOW (window)) {
			/* We now have the metadata to set up the window position, etc */
			setup_new_spatial_window (slot, file);
		}
		create_content_view (slot, view_id);
		g_free (view_id);
	} else {
		display_view_selection_failure (window, file,
						location, error);

		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (window))) {
			/* Destroy never-had-a-chance-to-be-seen window. This case
			 * happens when a new window cannot display its initial URI. 
			 */
			/* if this is the only window, we don't want to quit, so we redirect it to home */
			if (nautilus_application_get_n_windows () <= 1) {
				g_assert (nautilus_application_get_n_windows () == 1);

				/* Make sure we re-use this window */
				if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
					NAUTILUS_SPATIAL_WINDOW (window)->affect_spatial_window_on_next_location_change = TRUE;
				}
				/* the user could have typed in a home directory that doesn't exist,
				   in which case going home would cause an infinite loop, so we
				   better test for that */

				if (!nautilus_is_root_directory (location)) {
					if (!nautilus_is_home_directory (location)) {	
						nautilus_window_slot_go_home (NAUTILUS_WINDOW (window)->details->active_slot, FALSE);
					} else {
						GFile *root;

						root = g_file_new_for_path ("/");
						/* the last fallback is to go to a known place that can't be deleted! */
						nautilus_window_slot_go_to (NAUTILUS_WINDOW (window)->details->active_slot, location, FALSE);
						g_object_unref (root);
					}
				} else {
					gtk_object_destroy (GTK_OBJECT (window));
				}
			} else {
				/* Since this is a window, destroying it will also unref it. */
				gtk_object_destroy (GTK_OBJECT (window));
			}
		} else {
			/* Clean up state of already-showing window */
			end_location_change (slot);

			/* TODO? shouldn't we call
			 *   cancel_viewed_file_changed_callback (slot);
			 * at this point, or in end_location_change()
			 */

			/* We disconnected this, so we need to re-connect it */
			viewed_file = nautilus_file_get (slot->location);
			nautilus_window_slot_set_viewed_file (slot, viewed_file);
			nautilus_file_monitor_add (viewed_file, &slot->viewed_file, 0);
			g_signal_connect_object (viewed_file, "changed",
						 G_CALLBACK (viewed_file_changed_callback), slot, 0);
			nautilus_file_unref (viewed_file);
			
			/* Leave the location bar showing the bad location that the user
			 * typed (or maybe achieved by dragging or something). Many times
			 * the mistake will just be an easily-correctable typo. The user
			 * can choose "Refresh" to get the original URI back in the location bar.
			 */
		}
	}
	
	nautilus_file_unref (file);
}

/* Load a view into the window, either reusing the old one or creating
 * a new one. This happens when you want to load a new location, or just
 * switch to a different view.
 * If pending_location is set we're loading a new location and
 * pending_location/selection will be used. If not, we're just switching
 * view, and the current location will be used.
 */
static void
create_content_view (NautilusWindowSlot *slot,
		     const char *view_id)
{
	NautilusWindow *window;
        NautilusView *view;
	GList *selection;

	window = slot->window;

 	/* FIXME bugzilla.gnome.org 41243: 
	 * We should use inheritance instead of these special cases
	 * for the desktop window.
	 */
        if (NAUTILUS_IS_DESKTOP_WINDOW (window)) {
        	/* We force the desktop to use a desktop_icon_view. It's simpler
        	 * to fix it here than trying to make it pick the right view in
        	 * the first place.
        	 */
		view_id = NAUTILUS_DESKTOP_ICON_VIEW_IID;
	} 
        
        if (slot->content_view != NULL &&
	    eel_strcmp (nautilus_view_get_view_id (slot->content_view),
			view_id) == 0) {
                /* reuse existing content view */
                view = slot->content_view;
                slot->new_content_view = view;
        	g_object_ref (view);
        } else {
                /* create a new content view */
		view = nautilus_view_factory_create (view_id,
						     NAUTILUS_WINDOW_SLOT_INFO (slot));

                eel_accessibility_set_name (view, _("Content View"));
                eel_accessibility_set_description (view, _("View of the current folder"));

                slot->new_content_view = view;
		nautilus_window_slot_connect_content_view (slot, slot->new_content_view);
        }

	/* Actually load the pending location and selection: */

        if (slot->pending_location != NULL) {
		load_new_location (slot,
				   slot->pending_location,
				   slot->pending_selection,
				   FALSE,
				   TRUE);

		eel_g_object_list_free (slot->pending_selection);
		slot->pending_selection = NULL;
	} else if (slot->location != NULL) {
		selection = nautilus_view_get_selection (slot->content_view);
		load_new_location (slot,
				   slot->location,
				   selection,
				   FALSE,
				   TRUE);
		eel_g_object_list_free (selection);
	} else {
		/* Something is busted, there was no location to load.
		   Just load the homedir. */
		nautilus_window_slot_go_home (slot, FALSE);
		
	}
}

static void
load_new_location (NautilusWindowSlot *slot,
		   GFile *location,
		   GList *selection,
		   gboolean tell_current_content_view,
		   gboolean tell_new_content_view)
{
	NautilusWindow *window;
	GList *selection_copy;
	NautilusView *view;
	char *uri;

	g_assert (slot != NULL);
	g_assert (location != NULL);

	window = slot->window;
	g_assert (NAUTILUS_IS_WINDOW (window));

	selection_copy = eel_g_object_list_copy (selection);

	view = NULL;
	
	/* Note, these may recurse into report_load_underway */
        if (slot->content_view != NULL && tell_current_content_view) {
		view = slot->content_view;
		uri = g_file_get_uri (location);
		nautilus_view_load_location (slot->content_view, uri);
		g_free (uri);
        }
	
        if (slot->new_content_view != NULL && tell_new_content_view &&
	    (!tell_current_content_view ||
	     slot->new_content_view != slot->content_view) ) {
		view = slot->new_content_view;
		uri = g_file_get_uri (location);
		nautilus_view_load_location (slot->new_content_view, uri);
		g_free (uri);
        }
	if (view != NULL) {
		/* slot->new_content_view might have changed here if
		   report_load_underway was called from load_location */
		nautilus_view_set_selection (view, selection_copy);
	}
	
        eel_g_object_list_free (selection_copy);
}

/* A view started to load the location its viewing, either due to
 * a load_location request, or some internal reason. Expect
 * a matching load_compete later
 */
void
nautilus_window_report_load_underway (NautilusWindow *window,
				      NautilusView *view)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW (window));

	if (window->details->temporarily_ignore_view_signals) {
		return;
	}

	slot = nautilus_window_get_slot_for_view (window, view);
	g_assert (slot != NULL);

	if (view == slot->new_content_view) {
		location_has_really_changed (slot);
	} else {
		nautilus_window_slot_set_allow_stop (slot, TRUE);
	}
}

static void
nautilus_window_emit_location_change (NautilusWindow *window,
				      GFile *location)
{
	char *uri;

	uri = g_file_get_uri (location);
	g_signal_emit_by_name (window, "loading_uri", uri);
	g_free (uri);
}

/* reports location change to window's "loading-uri" clients, i.e.
 * sidebar panels [used when switching tabs]. It will emit the pending
 * location, or the existing location if none is pending.
 */
void
nautilus_window_report_location_change (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	GFile *location;

	g_assert (NAUTILUS_IS_WINDOW (window));

	slot = window->details->active_slot;
	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));

	location = NULL;

	if (slot->pending_location != NULL) {
		location = slot->pending_location;
	}

	if (location == NULL && slot->location != NULL) {
		location = slot->location;
	}

	if (location != NULL) {
		nautilus_window_emit_location_change (window, location);
	}
}

/* This is called when we have decided we can actually change to the new view/location situation. */
static void
location_has_really_changed (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	GtkWidget *widget;
	GFile *location_copy;

	window = slot->window;

	if (slot->new_content_view != NULL) {
		widget = nautilus_view_get_widget (slot->new_content_view);
		/* Switch to the new content view. */
		if (widget->parent == NULL) {
			if (slot->content_view != NULL) {
				nautilus_window_slot_disconnect_content_view (slot, slot->content_view);
			}
			nautilus_window_slot_set_content_view_widget (slot, slot->new_content_view);
		}
		g_object_unref (slot->new_content_view);
		slot->new_content_view = NULL;
	}

      if (slot->pending_location != NULL) {
		/* Tell the window we are finished. */
		update_for_new_location (slot);
	}

	location_copy = NULL;
	if (slot->location != NULL) {
		location_copy = g_object_ref (slot->location);
	}

	free_location_change (slot);

	if (location_copy != NULL) {
		if (slot == nautilus_window_get_active_slot (window)) {
			nautilus_window_emit_location_change (window, location_copy);
		}

		g_object_unref (location_copy);
	}
}

static void
slot_add_extension_extra_widgets (NautilusWindowSlot *slot)
{
	GList *providers, *l;
	GtkWidget *widget;
	char *uri;
	
	providers = nautilus_module_get_extensions_for_type (NAUTILUS_TYPE_LOCATION_WIDGET_PROVIDER);

	uri = g_file_get_uri (slot->location);
	for (l = providers; l != NULL; l = l->next) {
		NautilusLocationWidgetProvider *provider;
		
		provider = NAUTILUS_LOCATION_WIDGET_PROVIDER (l->data);
		widget = nautilus_location_widget_provider_get_widget (provider, uri, GTK_WIDGET (slot->window));
		if (widget != NULL) {
			nautilus_window_slot_add_extra_location_widget (slot, widget);
		}
	}
	g_free (uri);

	nautilus_module_extension_list_free (providers);
}

static void
nautilus_window_slot_show_x_content_bar (NautilusWindowSlot *slot, GMount *mount, const char **x_content_types)
{
	unsigned int n;

	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));

	for (n = 0; x_content_types[n] != NULL; n++) {
		GAppInfo *default_app;

		/* skip blank media; the burn:/// location will provide it's own cluebar */
		if (g_str_has_prefix (x_content_types[n], "x-content/blank-")) {
			continue;
		}

		/* don't show the cluebar for windows software */
		if (g_content_type_is_a (x_content_types[n], "x-content/win32-software")) {
			continue;
		}

		/* only show the cluebar if a default app is available */
		default_app = g_app_info_get_default_for_type (x_content_types[n], FALSE);
		if (default_app != NULL)  {
			GtkWidget *bar;
			bar = nautilus_x_content_bar_new (mount, x_content_types[n]);
			gtk_widget_show (bar);
			nautilus_window_slot_add_extra_location_widget (slot, bar);
			g_object_unref (default_app);
		}
	}
}

static void
nautilus_window_slot_show_trash_bar (NautilusWindowSlot *slot)
{
	GtkWidget *bar;

	bar = nautilus_trash_bar_new ();
	gtk_widget_show (bar);

	nautilus_window_slot_add_extra_location_widget (slot, bar);
}

typedef struct {
	NautilusWindowSlot *slot;
	GCancellable *cancellable;
	GMount *mount;
} FindMountData;

static void
found_content_type_cb (const char **x_content_types, FindMountData *data)
{
	NautilusWindowSlot *slot;
	
	if (g_cancellable_is_cancelled (data->cancellable)) {
		goto out;
	}

	slot = data->slot;

	if (x_content_types != NULL && x_content_types[0] != NULL) {
		nautilus_window_slot_show_x_content_bar (slot, data->mount, x_content_types);
	}

	slot->find_mount_cancellable = NULL;

 out:
	g_object_unref (data->mount);
	g_object_unref (data->cancellable);
	g_free (data);
}

static void
found_mount_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	FindMountData *data = user_data;
	GMount *mount;
	NautilusWindowSlot *slot;

	if (g_cancellable_is_cancelled (data->cancellable)) {
		goto out;
	}

	slot = data->slot;
	
	mount = g_file_find_enclosing_mount_finish (G_FILE (source_object),
						    res,
						    NULL);
	if (mount != NULL) {
		data->mount = mount;
		nautilus_autorun_get_x_content_types_for_mount_async (mount,
								      (NautilusAutorunGetContent)found_content_type_cb,
								      data->cancellable,
								      data);
		return;
	}
	
	data->slot->find_mount_cancellable = NULL;

 out:
	g_object_unref (data->cancellable);
	g_free (data);
}

void
nautilus_window_sync_location_widgets (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	char *uri;

	slot = window->details->active_slot;

	if (NAUTILUS_IS_NAVIGATION_WINDOW (window)) {
		NautilusNavigationWindowSlot *navigation_slot;

		navigation_slot = NAUTILUS_NAVIGATION_WINDOW_SLOT (slot);

		/* Check if the back and forward buttons need enabling or disabling. */
		update_up_button (window);
		nautilus_navigation_window_allow_back (NAUTILUS_NAVIGATION_WINDOW (window),
						       navigation_slot->back_list != NULL);
		nautilus_navigation_window_allow_forward (NAUTILUS_NAVIGATION_WINDOW (window),
							  navigation_slot->forward_list != NULL);

		/* Change the location bar and path bar to match the current location. */
		if (slot->location != NULL) {
			/* this may be NULL if we just created the
 			 * slot */
			uri = nautilus_window_slot_get_location_uri (slot);
			nautilus_navigation_bar_set_location (NAUTILUS_NAVIGATION_BAR (NAUTILUS_NAVIGATION_WINDOW (window)->navigation_bar),
							      uri);
			g_free (uri);
			nautilus_path_bar_set_path (NAUTILUS_PATH_BAR (NAUTILUS_NAVIGATION_WINDOW (window)->path_bar),
						    slot->location);
		}
	}

	if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
		/* Change the location button to match the current location. */
		nautilus_spatial_window_set_location_button (NAUTILUS_SPATIAL_WINDOW (window),
							     slot->location);
	}
}

/* Handle the changes for the NautilusWindow itself. */
static void
update_for_new_location (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
        GFile *new_location;
        NautilusFile *file;
	NautilusDirectory *directory;
	gboolean location_really_changed;
	FindMountData *data;

	window = slot->window;

	new_location = slot->pending_location;
	slot->pending_location = NULL;

	set_displayed_location (slot, new_location);

	update_history (slot, slot->location_change_type, new_location);

	location_really_changed =
		slot->location == NULL ||
		!g_file_equal (slot->location, new_location);
		
        /* Set the new location. */
	if (slot->location) {
		g_object_unref (slot->location);
	}
	slot->location = new_location;

        /* Create a NautilusFile for this location, so we can catch it
         * if it goes away.
         */
	cancel_viewed_file_changed_callback (slot);
	file = nautilus_file_get (slot->location);
	nautilus_window_slot_set_viewed_file (slot, file);
	slot->viewed_file_seen = !nautilus_file_is_not_yet_confirmed (file);
	slot->viewed_file_in_trash = nautilus_file_is_in_trash (file);
	nautilus_file_monitor_add (file, &slot->viewed_file, 0);
	g_signal_connect_object (file, "changed",
				 G_CALLBACK (viewed_file_changed_callback), slot, 0);
        nautilus_file_unref (file);

	if (slot == window->details->active_slot) {
		/* Check if we can go up. */
		update_up_button (window);
	}

	if (slot == window->details->active_slot) {
		nautilus_window_sync_zoom_widgets (window);
		/* Set up the content view menu for this new location. */
		nautilus_window_load_view_as_menus (window);

		/* Load menus from nautilus extensions for this location */
		nautilus_window_load_extension_menus (window);
	}

	if (location_really_changed) {
		nautilus_window_slot_remove_extra_location_widgets (slot);
		
		directory = nautilus_directory_get (slot->location);

		nautilus_window_slot_update_query_editor (slot);

		if (nautilus_directory_is_in_trash (directory)) {
			nautilus_window_slot_show_trash_bar (slot);
		}

		/* need the mount to determine if we should put up the x-content cluebar */
		if (slot->find_mount_cancellable != NULL) {
			g_cancellable_cancel (slot->find_mount_cancellable);
			slot->find_mount_cancellable = NULL;
		}
		
		data = g_new (FindMountData, 1);
		data->slot = slot;
		data->cancellable = g_cancellable_new ();
		data->mount = NULL;

		slot->find_mount_cancellable = data->cancellable;
		g_file_find_enclosing_mount_async (slot->location,
						   G_PRIORITY_DEFAULT, 
						   data->cancellable,
						   found_mount_cb,
						   data);

		nautilus_directory_unref (directory);

		slot_add_extension_extra_widgets (slot);
	}

	if (slot == window->details->active_slot) {
		nautilus_window_sync_location_widgets (window);

		if (location_really_changed) {
			nautilus_window_sync_search_widgets (window);
		}

		if (NAUTILUS_IS_NAVIGATION_WINDOW (window)) {
			nautilus_navigation_window_load_extension_toolbar_items (NAUTILUS_NAVIGATION_WINDOW (window));
		}
	}
}

/* A location load previously announced by load_underway
 * has been finished */
void
nautilus_window_report_load_complete (NautilusWindow *window,
				      NautilusView *view)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW (window));

	if (window->details->temporarily_ignore_view_signals) {
		return;
	}

	slot = nautilus_window_get_slot_for_view (window, view);
	g_assert (slot != NULL);

	/* Only handle this if we're expecting it.
	 * Don't handle it if its from an old view we've switched from */
	if (view == slot->content_view) {
		if (slot->pending_scroll_to != NULL) {
			nautilus_view_scroll_to_file (slot->content_view,
						      slot->pending_scroll_to);
		}
		end_location_change (slot);
	}
}

static void
end_location_change (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	char *uri;

	window = slot->window;

	uri = nautilus_window_slot_get_location_uri (slot);
	if (uri) {
		nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
				    "finished loading window %p: %s", window, uri);
		g_free (uri);
	}

	nautilus_window_slot_set_allow_stop (slot, FALSE);

        /* Now we can free pending_scroll_to, since the load_complete
         * callback already has been emitted.
         */
	g_free (slot->pending_scroll_to);
	slot->pending_scroll_to = NULL;

	free_location_change (slot);
}

static void
free_location_change (NautilusWindowSlot *slot)
{
	NautilusWindow *window;

	window = slot->window;
	g_assert (NAUTILUS_IS_WINDOW (window));

	if (slot->pending_location) {
		g_object_unref (slot->pending_location);
	}
        slot->pending_location = NULL;

	eel_g_object_list_free (slot->pending_selection);
	slot->pending_selection = NULL;
	
        /* Don't free pending_scroll_to, since thats needed until
         * the load_complete callback.
         */

	if (slot->mount_cancellable != NULL) {
		g_cancellable_cancel (slot->mount_cancellable);
		slot->mount_cancellable = NULL;
	}

        if (slot->determine_view_file != NULL) {
		nautilus_file_cancel_call_when_ready
			(slot->determine_view_file,
			 got_file_info_for_view_selection_callback, slot);
                slot->determine_view_file = NULL;
        }

        if (slot->new_content_view != NULL) {
		window->details->temporarily_ignore_view_signals = TRUE;
		nautilus_view_stop_loading (slot->new_content_view);
		window->details->temporarily_ignore_view_signals = FALSE;

		nautilus_window_slot_disconnect_content_view (slot, slot->new_content_view);
        	g_object_unref (slot->new_content_view);
                slot->new_content_view = NULL;
        }
}

static void
cancel_location_change (NautilusWindowSlot *slot)
{
	GList *selection;
	
        if (slot->pending_location != NULL
            && slot->location != NULL
            && slot->content_view != NULL) {

                /* No need to tell the new view - either it is the
                 * same as the old view, in which case it will already
                 * be told, or it is the very pending change we wish
                 * to cancel.
                 */
		selection = nautilus_view_get_selection (slot->content_view);
                load_new_location (slot,
				   slot->location,
				   selection,
				   TRUE,
				   FALSE);
		eel_g_object_list_free (selection);
        }

        end_location_change (slot);
}

void
nautilus_window_report_view_failed (NautilusWindow *window,
				    NautilusView *view)
{
	NautilusWindowSlot *slot;
	gboolean do_close_window;
	GFile *fallback_load_location;

	if (window->details->temporarily_ignore_view_signals) {
		return;
	}

	slot = nautilus_window_get_slot_for_view (window, view);
	g_assert (slot != NULL);
  
        g_warning ("A view failed. The UI will handle this with a dialog but this should be debugged.");

	do_close_window = FALSE;
	fallback_load_location = NULL;
	
	if (view == slot->content_view) {
		nautilus_window_slot_disconnect_content_view (slot, view);
                nautilus_window_slot_set_content_view_widget (slot, NULL);

                report_current_content_view_failure_to_user (slot);
        } else {
		/* Only report error on first try */
		if (slot->location_change_type != NAUTILUS_LOCATION_CHANGE_FALLBACK) {
			report_nascent_content_view_failure_to_user (slot, view);

			fallback_load_location = g_object_ref (slot->pending_location);
		} else {
			if (!GTK_WIDGET_VISIBLE (window)) {
				do_close_window = TRUE;
			}
		}
        }
        
        cancel_location_change (slot);

	if (fallback_load_location != NULL) {
		/* We loose the pending selection change here, but who cares... */
		begin_location_change (slot, fallback_load_location, NULL,
				       NAUTILUS_LOCATION_CHANGE_FALLBACK, 0, NULL);
		g_object_unref (fallback_load_location);
	}

	if (do_close_window) {
		gtk_widget_destroy (GTK_WIDGET (window));
	}
}

static void
display_view_selection_failure (NautilusWindow *window, NautilusFile *file,
				GFile *location, GError *error)
{
	char *full_uri_for_display;
	char *uri_for_display;
	char *error_message;
	char *detail_message;
	char *scheme_string;
	GtkDialog *dialog;

	/* Some sort of failure occurred. How 'bout we tell the user? */
	full_uri_for_display = g_file_get_parse_name (location);
	/* Truncate the URI so it doesn't get insanely wide. Note that even
	 * though the dialog uses wrapped text, if the URI doesn't contain
	 * white space then the text-wrapping code is too stupid to wrap it.
	 */
	uri_for_display = eel_str_middle_truncate
		(full_uri_for_display, MAX_URI_IN_DIALOG_LENGTH);
	g_free (full_uri_for_display);

	error_message = NULL;
	detail_message = NULL;
	if (error == NULL) {
		if (nautilus_file_is_directory (file)) {
			error_message = g_strdup_printf
				(_("Could not display \"%s\"."),
				 uri_for_display);
			detail_message = g_strdup 
				(_("Nautilus has no installed viewer capable of displaying the folder."));
		} else {
			error_message = g_strdup_printf
				(_("Could not display \"%s\"."),
				 uri_for_display);
			detail_message = g_strdup 
				(_("The location is not a folder."));
		}
	} else if (error->domain == G_IO_ERROR) {
		switch (error->code) {
		case G_IO_ERROR_NOT_FOUND:
			error_message = g_strdup_printf
				(_("Could not find \"%s\"."), 
				 uri_for_display);
			detail_message = g_strdup 
				(_("Please check the spelling and try again."));
			break;
		case G_IO_ERROR_NOT_SUPPORTED:
			scheme_string = g_file_get_uri_scheme (location);
				
			error_message = g_strdup_printf (_("Could not display \"%s\"."),
							 uri_for_display);
			if (scheme_string != NULL) {
				detail_message = g_strdup_printf (_("Nautilus cannot handle \"%s\" locations."),
								  scheme_string);
			} else {
				detail_message = g_strdup (_("Nautilus cannot handle this kind of location."));
			}
			g_free (scheme_string);
			break;
		case G_IO_ERROR_NOT_MOUNTED:
			error_message = g_strdup_printf (_("Could not display \"%s\"."),
							 uri_for_display);
			detail_message = g_strdup (_("Unable to mount the location."));
			break;
			
		case G_IO_ERROR_PERMISSION_DENIED:
			error_message = g_strdup_printf (_("Could not display \"%s\"."),
							 uri_for_display);
			detail_message = g_strdup (_("Access was denied."));
			break;
			
		case G_IO_ERROR_HOST_NOT_FOUND:
			/* This case can be hit for user-typed strings like "foo" due to
			 * the code that guesses web addresses when there's no initial "/".
			 * But this case is also hit for legitimate web addresses when
			 * the proxy is set up wrong.
			 */
			error_message = g_strdup_printf (_("Could not display \"%s\", because the host could not be found."),
							 uri_for_display);
			detail_message = g_strdup (_("Check that the spelling is correct and that your proxy settings are correct."));
			break;
		case G_IO_ERROR_CANCELLED:
			g_free (uri_for_display);
			return;
			
		default:
			break;
		}
	}
	
	if (error_message == NULL) {
		error_message = g_strdup_printf (_("Could not display \"%s\"."),
						 uri_for_display);
		detail_message = g_strdup_printf (_("Error: %s\nPlease select another viewer and try again."), error->message);
	}
	
	dialog = eel_show_error_dialog (error_message, detail_message, NULL);
	
	g_free (uri_for_display);
	g_free (error_message);
	g_free (detail_message);
}


void
nautilus_window_slot_stop_loading (NautilusWindowSlot *slot)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (slot->window);
	g_assert (NAUTILUS_IS_WINDOW (window));

	nautilus_view_stop_loading (slot->content_view);
	
	if (slot->new_content_view != NULL) {
		window->details->temporarily_ignore_view_signals = TRUE;
		nautilus_view_stop_loading (slot->new_content_view);
		window->details->temporarily_ignore_view_signals = FALSE;
	}

        cancel_location_change (slot);
}

void
nautilus_window_slot_set_content_view (NautilusWindowSlot *slot,
				       const char *id)
{
	NautilusWindow *window;
	NautilusFile *file;
	char *uri;

	g_assert (slot != NULL);
	g_assert (slot->location != NULL);
	g_assert (id != NULL);

	window = slot->window;
	g_assert (NAUTILUS_IS_WINDOW (window));
  
	uri = nautilus_window_slot_get_location_uri (slot);
	nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
			    "change view of window %p: \"%s\" to \"%s\"",
			    window, uri, id);
	g_free (uri);

	if (nautilus_window_slot_content_view_matches_iid (slot, id)) {
        	return;
        }

        end_location_change (slot);

	file = nautilus_file_get (slot->location);
	nautilus_file_set_metadata 
		(file, NAUTILUS_METADATA_KEY_DEFAULT_VIEW, NULL, id);
        nautilus_file_unref (file);
        
        nautilus_window_slot_set_allow_stop (slot, TRUE);

        if (nautilus_view_get_selection_count (slot->content_view) == 0) {
                /* If there is no selection, queue a scroll to the same icon that
                 * is currently visible */
                slot->pending_scroll_to = nautilus_view_get_first_visible_file (slot->content_view);
        }
	slot->location_change_type = NAUTILUS_LOCATION_CHANGE_RELOAD;
	
        create_content_view (slot, id);
}

void
nautilus_window_manage_views_close_slot (NautilusWindow *window,
					 NautilusWindowSlot *slot)
{
	if (slot->content_view != NULL) {
		nautilus_window_slot_disconnect_content_view (slot, slot->content_view);
	}

	free_location_change (slot);
	cancel_viewed_file_changed_callback (slot);
}

void
nautilus_navigation_window_back_or_forward (NautilusNavigationWindow *window, 
                                            gboolean back, guint distance, gboolean new_tab)
{
	NautilusWindowSlot *slot;
	NautilusNavigationWindowSlot *navigation_slot;
	GList *list;
	GFile *location;
        guint len;
        NautilusBookmark *bookmark;

	slot = NAUTILUS_WINDOW (window)->details->active_slot;
	navigation_slot = (NautilusNavigationWindowSlot *) slot;
	list = back ? navigation_slot->back_list : navigation_slot->forward_list;

        len = (guint) g_list_length (list);

        /* If we can't move in the direction at all, just return. */
        if (len == 0)
                return;

        /* If the distance to move is off the end of the list, go to the end
           of the list. */
        if (distance >= len)
                distance = len - 1;

        bookmark = g_list_nth_data (list, distance);
	location = nautilus_bookmark_get_location (bookmark);

	if (new_tab) {
		nautilus_window_slot_open_location_full (slot, location,
							 NAUTILUS_WINDOW_OPEN_ACCORDING_TO_MODE,
							 NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB,
							 NULL);
	} else {
        	char *scroll_pos;
		
		scroll_pos = nautilus_bookmark_get_scroll_pos (bookmark);
		begin_location_change
			(slot,
			 location, NULL,
			 back ? NAUTILUS_LOCATION_CHANGE_BACK : NAUTILUS_LOCATION_CHANGE_FORWARD,
			 distance,
			 scroll_pos);

		g_free (scroll_pos);
	}

	g_object_unref (location);
}

/* reload the contents of the window */
void
nautilus_window_slot_reload (NautilusWindowSlot *slot)
{
	GFile *location;
        char *current_pos;
	GList *selection;

	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));

	if (slot->location == NULL) {
		return;
	}
	
	/* peek_slot_field (window, location) can be free'd during the processing
	 * of begin_location_change, so make a copy
	 */
	location = g_object_ref (slot->location);
	current_pos = NULL;
	selection = NULL;
	if (slot->content_view != NULL) {
		current_pos = nautilus_view_get_first_visible_file (slot->content_view);
		selection = nautilus_view_get_selection (slot->content_view);
	}
	begin_location_change
		(slot, location, selection,
		 NAUTILUS_LOCATION_CHANGE_RELOAD, 0, current_pos);
        g_free (current_pos);
	g_object_unref (location);
	eel_g_object_list_free (selection);
}

void
nautilus_window_reload (NautilusWindow *window)
{
	g_assert (NAUTILUS_IS_WINDOW (window));

	nautilus_window_slot_reload (window->details->active_slot);
}

