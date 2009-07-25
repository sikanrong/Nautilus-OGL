/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nautilus-file-utilities.c - implementation of file manipulation routines.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: John Sullivan <sullivan@eazel.com>
*/

#include <config.h>
#include "nautilus-file-utilities.h"

#include "nautilus-global-preferences.h"
#include "nautilus-lib-self-check-functions.h"
#include "nautilus-metadata.h"
#include "nautilus-file.h"
#include "nautilus-search-directory.h"
#include "nautilus-signaller.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-string.h>
#include <eel/eel-debug.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <unistd.h>
#include <stdlib.h>

#define NAUTILUS_USER_DIRECTORY_NAME ".nautilus"
#define DEFAULT_NAUTILUS_DIRECTORY_MODE (0755)

#define DESKTOP_DIRECTORY_NAME "Desktop"
#define LEGACY_DESKTOP_DIRECTORY_NAME ".gnome-desktop"
#define DEFAULT_DESKTOP_DIRECTORY_MODE (0755)

static void update_xdg_dir_cache (void);
static void schedule_user_dirs_changed (void);
static void desktop_dir_changed (void);
static GFile *nautilus_find_file_insensitive_next (GFile *parent, const gchar *name);

char *
nautilus_compute_title_for_location (GFile *location)
{
	NautilusFile *file;
	char *title;

	/* TODO-gio: This doesn't really work all that great if the
	   info about the file isn't known atm... */
	
	title = NULL;
	if (location) {
		file = nautilus_file_get (location);
		title = nautilus_file_get_description (file);
		if (title == NULL) {
			title = nautilus_file_get_display_name (file);
		}
		nautilus_file_unref (file);
	}

	if (title == NULL) {
		title = g_strdup ("");
	}
	
	return title;
}


/**
 * nautilus_get_user_directory:
 * 
 * Get the path for the directory containing nautilus settings.
 *
 * Return value: the directory path.
 **/
char *
nautilus_get_user_directory (void)
{
	char *user_directory = NULL;

	user_directory = g_build_filename (g_get_home_dir (),
					   NAUTILUS_USER_DIRECTORY_NAME,
					   NULL);
	
	if (!g_file_test (user_directory, G_FILE_TEST_EXISTS)) {
		g_mkdir (user_directory, DEFAULT_NAUTILUS_DIRECTORY_MODE);
		/* FIXME bugzilla.gnome.org 41286: 
		 * How should we handle the case where this mkdir fails? 
		 * Note that nautilus_application_startup will refuse to launch if this 
		 * directory doesn't get created, so that case is OK. But the directory 
		 * could be deleted after Nautilus was launched, and perhaps
		 * there is some bad side-effect of not handling that case.
		 */
	}

	return user_directory;
}

/**
 * nautilus_get_accel_map_file:
 * 
 * Get the path for the filename containing nautilus accelerator map.
 * The filename need not exist.
 *
 * Return value: the filename path, or NULL if the home directory could not be found
 **/
char *
nautilus_get_accel_map_file (void)
{
	const gchar *home;

	home = g_get_home_dir();
	if (home != NULL) {
		return g_build_filename (home, ".gnome2/accels/nautilus", NULL);
	}

	return NULL;
}

typedef struct {
	char *type;
	char *path;
	NautilusFile *file;
} XdgDirEntry;


static XdgDirEntry *
parse_xdg_dirs (const char *config_file)
{
  GArray *array;
  char *config_file_free = NULL;
  XdgDirEntry dir;
  char *data;
  char **lines;
  char *p, *d;
  int i;
  char *type_start, *type_end;
  char *value, *unescaped;
  gboolean relative;

  array = g_array_new (TRUE, TRUE, sizeof (XdgDirEntry));
  
  if (config_file == NULL)
    {
      config_file_free = g_build_filename (g_get_user_config_dir (),
					   "user-dirs.dirs", NULL);
      config_file = (const char *)config_file_free;
    }

  if (g_file_get_contents (config_file, &data, NULL, NULL))
    {
      lines = g_strsplit (data, "\n", 0);
      g_free (data);
      for (i = 0; lines[i] != NULL; i++)
	{
	  p = lines[i];
	  while (g_ascii_isspace (*p))
	    p++;
      
	  if (*p == '#')
	    continue;
      
	  value = strchr (p, '=');
	  if (value == NULL)
	    continue;
	  *value++ = 0;
      
	  g_strchug (g_strchomp (p));
	  if (!g_str_has_prefix (p, "XDG_"))
	    continue;
	  if (!g_str_has_suffix (p, "_DIR"))
	    continue;
	  type_start = p + 4;
	  type_end = p + strlen (p) - 4;
      
	  while (g_ascii_isspace (*value))
	    value++;
      
	  if (*value != '"')
	    continue;
	  value++;
      
	  relative = FALSE;
	  if (g_str_has_prefix (value, "$HOME"))
	    {
	      relative = TRUE;
	      value += 5;
	      while (*value == '/')
		      value++;
	    }
	  else if (*value != '/')
	    continue;
	  
	  d = unescaped = g_malloc (strlen (value) + 1);
	  while (*value && *value != '"')
	    {
	      if ((*value == '\\') && (*(value + 1) != 0))
		value++;
	      *d++ = *value++;
	    }
	  *d = 0;
      
	  *type_end = 0;
	  dir.type = g_strdup (type_start);
	  if (relative)
	    {
	      dir.path = g_build_filename (g_get_home_dir (), unescaped, NULL);
	      g_free (unescaped);
	    }
	  else 
	    dir.path = unescaped;
      
	  g_array_append_val (array, dir);
	}
      
      g_strfreev (lines);
    }
  
  g_free (config_file_free);
  
  return (XdgDirEntry *)g_array_free (array, FALSE);
}

static XdgDirEntry *cached_xdg_dirs = NULL;
static GFileMonitor *cached_xdg_dirs_monitor = NULL;

static void
xdg_dir_changed (NautilusFile *file,
		 XdgDirEntry *dir)
{
	GFile *location, *dir_location;
	char *path;

	location = nautilus_file_get_location (file);
	dir_location = g_file_new_for_path (dir->path);
	if (!g_file_equal (location, dir_location)) {
		path = g_file_get_path (location);

		if (path) {
			char *argv[5];
			int i;
			
			g_free (dir->path);
			dir->path = path;

			i = 0;
			argv[i++] = "xdg-user-dirs-update";
			argv[i++] = "--set";
			argv[i++] = dir->type;
			argv[i++] = dir->path;
			argv[i++] = NULL;

			/* We do this sync, to avoid possible race-conditions
			   if multiple dirs change at the same time. Its
			   blocking the main thread, but these updates should
			   be very rare and very fast. */
			g_spawn_sync (NULL, 
				      argv, NULL,
				      G_SPAWN_SEARCH_PATH |
				      G_SPAWN_STDOUT_TO_DEV_NULL |
				      G_SPAWN_STDERR_TO_DEV_NULL,
				      NULL, NULL,
				      NULL, NULL, NULL, NULL);
			g_reload_user_special_dirs_cache ();
			schedule_user_dirs_changed ();
			desktop_dir_changed ();
			/* Icon might have changed */
			nautilus_file_invalidate_attributes (file, NAUTILUS_FILE_ATTRIBUTE_INFO);
		}
	}
	g_object_unref (location);
	g_object_unref (dir_location);
}

static void 
xdg_dir_cache_changed_cb (GFileMonitor  *monitor,
			  GFile *file,
			  GFile *other_file,
			  GFileMonitorEvent event_type)
{
	if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
	    event_type == G_FILE_MONITOR_EVENT_CREATED) {
		update_xdg_dir_cache ();
	}
}

static int user_dirs_changed_tag = 0;

static gboolean
emit_user_dirs_changed_idle (gpointer data)
{
	g_signal_emit_by_name (nautilus_signaller_get_current (),
			       "user_dirs_changed");
	user_dirs_changed_tag = 0;
	return FALSE;
}

static void
schedule_user_dirs_changed (void)
{
	if (user_dirs_changed_tag == 0) {
		user_dirs_changed_tag = g_idle_add (emit_user_dirs_changed_idle, NULL);
	}
}

static void
unschedule_user_dirs_changed (void)
{
	if (user_dirs_changed_tag != 0) {
		g_source_remove (user_dirs_changed_tag);
		user_dirs_changed_tag = 0;
	}
}

static void
free_xdg_dir_cache (void)
{
	int i;

	if (cached_xdg_dirs != NULL) {
		for (i = 0; cached_xdg_dirs[i].type != NULL; i++) {
			if (cached_xdg_dirs[i].file != NULL) {
				nautilus_file_monitor_remove (cached_xdg_dirs[i].file,
							      &cached_xdg_dirs[i]);
				g_signal_handlers_disconnect_by_func (cached_xdg_dirs[i].file,
								      G_CALLBACK (xdg_dir_changed),
								      &cached_xdg_dirs[i]);
				nautilus_file_unref (cached_xdg_dirs[i].file);
			}
			g_free (cached_xdg_dirs[i].type);
			g_free (cached_xdg_dirs[i].path);
		}
		g_free (cached_xdg_dirs);
	}
}

static void
destroy_xdg_dir_cache (void)
{
	free_xdg_dir_cache ();
	unschedule_user_dirs_changed ();
	desktop_dir_changed ();

	if (cached_xdg_dirs_monitor != NULL) {
		g_object_unref  (cached_xdg_dirs_monitor);
		cached_xdg_dirs_monitor = NULL;
	}
}

static void
update_xdg_dir_cache (void)
{
	GFile *file;
	char *config_file, *uri;
	int i;

	free_xdg_dir_cache ();
	g_reload_user_special_dirs_cache ();
	schedule_user_dirs_changed ();
	desktop_dir_changed ();

	cached_xdg_dirs = parse_xdg_dirs (NULL);
	
	for (i = 0 ; cached_xdg_dirs[i].type != NULL; i++) {
		cached_xdg_dirs[i].file = NULL;
		if (strcmp (cached_xdg_dirs[i].path, g_get_home_dir ()) != 0) {
			uri = g_filename_to_uri (cached_xdg_dirs[i].path, NULL, NULL);
			cached_xdg_dirs[i].file = nautilus_file_get_by_uri (uri);
			nautilus_file_monitor_add (cached_xdg_dirs[i].file,
						   &cached_xdg_dirs[i],
						   NAUTILUS_FILE_ATTRIBUTE_INFO);
			g_signal_connect (cached_xdg_dirs[i].file,
					  "changed", G_CALLBACK (xdg_dir_changed), &cached_xdg_dirs[i]);
			g_free (uri);
		}
	}

	if (cached_xdg_dirs_monitor == NULL) {
		config_file = g_build_filename (g_get_user_config_dir (),
						     "user-dirs.dirs", NULL);
		file = g_file_new_for_path (config_file);
		cached_xdg_dirs_monitor = g_file_monitor_file (file, 0, NULL, NULL);
		g_signal_connect (cached_xdg_dirs_monitor, "changed",
				  G_CALLBACK (xdg_dir_cache_changed_cb), NULL);
		g_object_unref (file);
		g_free (config_file);

		eel_debug_call_at_shutdown (destroy_xdg_dir_cache); 
	}
}

char *
nautilus_get_xdg_dir (const char *type)
{
	int i;

	if (cached_xdg_dirs == NULL) {
		update_xdg_dir_cache ();
	}

	for (i = 0 ; cached_xdg_dirs != NULL && cached_xdg_dirs[i].type != NULL; i++) {
		if (strcmp (cached_xdg_dirs[i].type, type) == 0) {
			return g_strdup (cached_xdg_dirs[i].path);
		}
	}
	if (strcmp ("DESKTOP", type) == 0) {
		return g_build_filename (g_get_home_dir (), DESKTOP_DIRECTORY_NAME, NULL);
	}
	if (strcmp ("TEMPLATES", type) == 0) {
		return g_build_filename (g_get_home_dir (), "Templates", NULL);
	}
	
	return g_strdup (g_get_home_dir ());
}

static char *
get_desktop_path (void)
{
	if (eel_preferences_get_boolean (NAUTILUS_PREFERENCES_DESKTOP_IS_HOME_DIR)) {
		return g_strdup (g_get_home_dir());
	} else {
		return nautilus_get_xdg_dir ("DESKTOP");
	}
}

/**
 * nautilus_get_desktop_directory:
 * 
 * Get the path for the directory containing files on the desktop.
 *
 * Return value: the directory path.
 **/
char *
nautilus_get_desktop_directory (void)
{
	char *desktop_directory;
	
	desktop_directory = get_desktop_path ();

	/* Don't try to create a home directory */
	if (!eel_preferences_get_boolean (NAUTILUS_PREFERENCES_DESKTOP_IS_HOME_DIR)) {
		if (!g_file_test (desktop_directory, G_FILE_TEST_EXISTS)) {
			g_mkdir (desktop_directory, DEFAULT_DESKTOP_DIRECTORY_MODE);
			/* FIXME bugzilla.gnome.org 41286: 
			 * How should we handle the case where this mkdir fails? 
			 * Note that nautilus_application_startup will refuse to launch if this 
			 * directory doesn't get created, so that case is OK. But the directory 
			 * could be deleted after Nautilus was launched, and perhaps
			 * there is some bad side-effect of not handling that case.
			 */
		}
	}

	return desktop_directory;
}

GFile *
nautilus_get_desktop_location (void)
{
	char *desktop_directory;
	GFile *res;
	
	desktop_directory = get_desktop_path ();

	res = g_file_new_for_path (desktop_directory);
	g_free (desktop_directory);
	return res;
}


/**
 * nautilus_get_desktop_directory_uri:
 * 
 * Get the uri for the directory containing files on the desktop.
 *
 * Return value: the directory path.
 **/
char *
nautilus_get_desktop_directory_uri (void)
{
	char *desktop_path;
	char *desktop_uri;
	
	desktop_path = nautilus_get_desktop_directory ();
	desktop_uri = g_filename_to_uri (desktop_path, NULL, NULL);
	g_free (desktop_path);

	return desktop_uri;
}

char *
nautilus_get_desktop_directory_uri_no_create (void)
{
	char *desktop_path;
	char *desktop_uri;
	
	desktop_path = get_desktop_path ();
	desktop_uri = g_filename_to_uri (desktop_path, NULL, NULL);
	g_free (desktop_path);

	return desktop_uri;
}

char *
nautilus_get_home_directory_uri (void)
{
	return  g_filename_to_uri (g_get_home_dir (), NULL, NULL);
}


gboolean
nautilus_should_use_templates_directory (void)
{
	char *dir;
	gboolean res;
	
	dir = nautilus_get_xdg_dir ("TEMPLATES");
	res = strcmp (dir, g_get_home_dir ()) != 0;
	g_free (dir);
	return res;
}

char *
nautilus_get_templates_directory (void)
{
	return nautilus_get_xdg_dir ("TEMPLATES");
}

void
nautilus_create_templates_directory (void)
{
	char *dir;

	dir = nautilus_get_templates_directory ();
	if (!g_file_test (dir, G_FILE_TEST_EXISTS)) {
		g_mkdir (dir, DEFAULT_NAUTILUS_DIRECTORY_MODE);
	}
	g_free (dir);
}

char *
nautilus_get_templates_directory_uri (void)
{
	char *directory, *uri;

	directory = nautilus_get_templates_directory ();
	uri = g_filename_to_uri (directory, NULL, NULL);
	g_free (directory);
	return uri;
}

char *
nautilus_get_searches_directory (void)
{
	char *user_dir;
	char *searches_dir;

	user_dir = nautilus_get_user_directory ();
	searches_dir = g_build_filename (user_dir, "searches", NULL);
	g_free (user_dir);
	
	if (!g_file_test (searches_dir, G_FILE_TEST_EXISTS))
		g_mkdir (searches_dir, DEFAULT_NAUTILUS_DIRECTORY_MODE);

	return searches_dir;
}

/* These need to be reset to NULL when desktop_is_home_dir changes */
static GFile *desktop_dir = NULL;
static GFile *desktop_dir_dir = NULL;
static char *desktop_dir_filename = NULL;
static gboolean desktop_dir_changed_callback_installed = FALSE;


static void
desktop_dir_changed (void)
{
	if (desktop_dir) {
		g_object_unref (desktop_dir);
	}
	if (desktop_dir_dir) {
		g_object_unref (desktop_dir_dir);
	}
	g_free (desktop_dir_filename);
	desktop_dir = NULL;
	desktop_dir_dir = NULL;
	desktop_dir_filename = NULL;
}

static void
desktop_dir_changed_callback (gpointer callback_data)
{
	desktop_dir_changed ();
}

static void
update_desktop_dir (void)
{
	char *path;
	char *dirname;

	path = get_desktop_path ();
	desktop_dir = g_file_new_for_path (path);
	
	dirname = g_path_get_dirname (path);
	desktop_dir_dir = g_file_new_for_path (dirname);
	g_free (dirname);
	desktop_dir_filename = g_path_get_basename (path);
	g_free (path);
}

gboolean
nautilus_is_home_directory_file (GFile *dir,
				 const char *filename)
{
	char *dirname;
	static GFile *home_dir_dir = NULL;
	static char *home_dir_filename = NULL;
	
	if (home_dir_dir == NULL) {
		dirname = g_path_get_dirname (g_get_home_dir ());
		home_dir_dir = g_file_new_for_path (dirname);
		g_free (dirname);
		home_dir_filename = g_path_get_basename (g_get_home_dir ());
	}

	return (g_file_equal (dir, home_dir_dir) &&
		strcmp (filename, home_dir_filename) == 0);
}

gboolean
nautilus_is_home_directory (GFile *dir)
{
	static GFile *home_dir = NULL;
	
	if (home_dir == NULL) {
		home_dir = g_file_new_for_path (g_get_home_dir ());
	}

	return g_file_equal (dir, home_dir);
}

gboolean
nautilus_is_root_directory (GFile *dir)
{
	static GFile *root_dir = NULL;
	
	if (root_dir == NULL) {
		root_dir = g_file_new_for_path ("/");
	}

	return g_file_equal (dir, root_dir);
}
		
		
gboolean
nautilus_is_desktop_directory_file (GFile *dir,
				    const char *file)
{

	if (!desktop_dir_changed_callback_installed) {
		eel_preferences_add_callback (NAUTILUS_PREFERENCES_DESKTOP_IS_HOME_DIR,
					      desktop_dir_changed_callback,
					      NULL);
		desktop_dir_changed_callback_installed = TRUE;
	}
		
	if (desktop_dir == NULL) {
		update_desktop_dir ();
	}

	return (g_file_equal (dir, desktop_dir_dir) &&
		strcmp (file, desktop_dir_filename) == 0);
}

gboolean
nautilus_is_desktop_directory (GFile *dir)
{

	if (!desktop_dir_changed_callback_installed) {
		eel_preferences_add_callback (NAUTILUS_PREFERENCES_DESKTOP_IS_HOME_DIR,
					      desktop_dir_changed_callback,
					      NULL);
		desktop_dir_changed_callback_installed = TRUE;
	}
		
	if (desktop_dir == NULL) {
		update_desktop_dir ();
	}

	return g_file_equal (dir, desktop_dir);
}


/**
 * nautilus_get_gmc_desktop_directory:
 * 
 * Get the path for the directory containing the legacy gmc desktop.
 *
 * Return value: the directory path.
 **/
char *
nautilus_get_gmc_desktop_directory (void)
{
	return g_build_filename (g_get_home_dir (), LEGACY_DESKTOP_DIRECTORY_NAME, NULL);
}

/**
 * nautilus_get_pixmap_directory
 * 
 * Get the path for the directory containing Nautilus pixmaps.
 *
 * Return value: the directory path.
 **/
char *
nautilus_get_pixmap_directory (void)
{
	return g_strdup (DATADIR "/pixmaps/nautilus");
}

/* FIXME bugzilla.gnome.org 42423: 
 * Callers just use this and dereference so we core dump if
 * pixmaps are missing. That is lame.
 */
char *
nautilus_pixmap_file (const char *partial_path)
{
	char *path;

	path = g_build_filename (DATADIR "/pixmaps/nautilus", partial_path, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		return path;
	} else {
		char *tmp;
		tmp = nautilus_get_pixmap_directory ();
		g_debug ("Failed to locate \"%s\" in Nautilus pixmap path \"%s\". Incomplete installation?", partial_path, tmp);
		g_free (tmp);
	}
	g_free (path);
	return NULL;
}

char *
nautilus_get_data_file_path (const char *partial_path)
{
	char *path;
	char *user_directory;

	/* first try the user's home directory */
	user_directory = nautilus_get_user_directory ();
	path = g_build_filename (user_directory, partial_path, NULL);
	g_free (user_directory);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		return path;
	}
	g_free (path);
	
	/* next try the shared directory */
	path = g_build_filename (NAUTILUS_DATADIR, partial_path, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		return path;
	}
	g_free (path);

	return NULL;
}

char *
nautilus_ensure_unique_file_name (const char *directory_uri,
				  const char *base_name,
				  const char *extension)
{
	GFileInfo *info;
	char *filename;
	GFile *dir, *child;
	int copy;
	char *res;

	dir = g_file_new_for_uri (directory_uri);

	info = g_file_query_info (dir, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL);
	if (info == NULL) {
		g_object_unref (dir);
		return NULL;
	}
	g_object_unref (info);

	filename = g_strdup_printf ("%s%s",
				    base_name,
				    extension);
	child = g_file_get_child (dir, filename);
	g_free (filename);
	
	copy = 1;
	while ((info = g_file_query_info (child, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL)) != NULL) {
		g_object_unref (info);
		g_object_unref (child);
		
		filename = g_strdup_printf ("%s-%d%s",
					    base_name,
					    copy,
					    extension);
		child = g_file_get_child (dir, filename);
		g_free (filename);
		
		copy++;
	}

	res = g_file_get_uri (child);
	g_object_unref (child);
	g_object_unref (dir);
	
	return res;
}

char *
nautilus_unique_temporary_file_name (void)
{
	const char *prefix = "/tmp/nautilus-temp-file";
	char *file_name;
	int fd;

	file_name = g_strdup_printf ("%sXXXXXX", prefix);

	fd = g_mkstemp (file_name); 
	if (fd == -1) {
		g_free (file_name);
		file_name = NULL;
	} else {
		close (fd);
	}
	
	return file_name;
}

GFile *
nautilus_find_existing_uri_in_hierarchy (GFile *location)
{
	GFileInfo *info;
	GFile *tmp;

	g_assert (location != NULL);

	location = g_object_ref (location);
	while (location != NULL) {
		info = g_file_query_info (location,
					  G_FILE_ATTRIBUTE_STANDARD_NAME,
					  0, NULL, NULL);
		g_object_unref (info);
		if (info != NULL) {
			return location;
		}
		tmp = location;
		location = g_file_get_parent (location);
		g_object_unref (tmp);
	}
	
	return location;
}

/**
 * nautilus_find_file_insensitive
 * 
 * Attempt to find a file case-insentively. If the path can be found, the
 * returned file maps directly to it. Otherwise, a file using the
 * originally-cased path is returned. This function performs might perform
 * I/O.
 * 
 * Return value: a #GFile to a child specified by @name.
 **/
GFile *
nautilus_find_file_insensitive (GFile *parent, const gchar *name)
{
	gchar **split_path;
	gchar *component;
	GFile *file, *next;
	gint i;
	
	split_path = g_strsplit (name, G_DIR_SEPARATOR_S, -1);
	
	file = g_object_ref (parent);
	
	for (i = 0; (component = split_path[i]) != NULL; i++) {
		if (!(next = nautilus_find_file_insensitive_next (file,
		                                                  component))) {
			/* File does not exist */
			g_object_unref (file);
			file = NULL;
			break;
		}
		g_object_unref (file);
		file = next;
	}
	g_strfreev (split_path);
	
	if (file) {
		return file;
	}
	return g_file_get_child (parent, name);
}

static GFile *
nautilus_find_file_insensitive_next (GFile *parent, const gchar *name)
{
	GFileEnumerator *children;
	GFileInfo *info;
	gboolean use_utf8, found;
	char *filename, *case_folded_name, *utf8_collation_key, *ascii_collation_key, *child_key;
	GFile *file;
	const char *child_name, *compare_key;

	/* First check the given version */
	file = g_file_get_child (parent, name);
	if (g_file_query_exists (file, NULL)) {
		return file;
	}
	g_object_unref (file);
	
	ascii_collation_key = g_ascii_strdown (name, -1);
	use_utf8 = g_utf8_validate (name, -1, NULL);
	utf8_collation_key = NULL;	
	if (use_utf8) {
		case_folded_name = g_utf8_casefold (name, -1);
		utf8_collation_key = g_utf8_collate_key (case_folded_name, -1);
		g_free (case_folded_name);
	}

	/* Enumerate and compare insensitive */
	filename = NULL;
	children = g_file_enumerate_children (parent,
	                                      G_FILE_ATTRIBUTE_STANDARD_NAME,
	                                      0, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL))) {
			child_name = g_file_info_get_name (info);
			
			if (use_utf8 && g_utf8_validate (child_name, -1, NULL)) {
				gchar *case_folded;
				
				case_folded = g_utf8_casefold (child_name, -1);
				child_key = g_utf8_collate_key (case_folded, -1);
				g_free (case_folded);
				compare_key = utf8_collation_key;
			} else {
				child_key = g_ascii_strdown (child_name, -1);
				compare_key = ascii_collation_key;
			}
			
			found = strcmp (child_key, compare_key) == 0;
			g_free (child_key);
			if (found) {
				filename = g_strdup (child_name);
				break;
			}
		}
		g_file_enumerator_close (children, NULL, NULL);
		g_object_unref (children);
	}
	
	g_free (ascii_collation_key);
	g_free (utf8_collation_key);
	
	if (filename) {
		file = g_file_get_child (parent, filename);
		g_free (filename);
		return file;
	}
	
	return NULL;
}

gboolean
nautilus_is_file_roller_installed (void)
{
	static int installed = - 1;

	if (installed < 0) {
		if (g_find_program_in_path ("file-roller")) {
			installed = 1;
		} else {
			installed = 0;
		}
	}

	return installed > 0 ? TRUE : FALSE;
}

/* Returns TRUE if the file is in XDG_DATA_DIRS or
   in "~/.gnome2/". This is used for deciding
   if a desktop file is "trusted" based on the path */
gboolean
nautilus_is_in_system_dir (GFile *file)
{
	const char * const * data_dirs; 
	char *path, *gnome2;
	int i;
	gboolean res;
	
	if (!g_file_is_native (file)) {
		return FALSE;
	}

	path = g_file_get_path (file);
	
	res = FALSE;

	data_dirs = g_get_system_data_dirs ();
	for (i = 0; path != NULL && data_dirs[i] != NULL; i++) {
		if (g_str_has_prefix (path, data_dirs[i])) {
			res = TRUE;
			break;
		}
		
	}

	if (!res) {
		/* Panel desktop files are here, trust them */
		gnome2 = g_build_filename (g_get_home_dir (), ".gnome2", NULL);
		if (g_str_has_prefix (path, gnome2)) {
			res = TRUE;
		}
		g_free (gnome2);
	}
	g_free (path);
	
	return res;
}


#if !defined (NAUTILUS_OMIT_SELF_CHECK)

void
nautilus_self_check_file_utilities (void)
{
}

#endif /* !NAUTILUS_OMIT_SELF_CHECK */
