/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fm-ogl-view.c - implementation of effects view of directory.

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2001, 2002 Anders Carlsson <andersca@gnu.org>
   
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

   Authors: Alex Pilafian <sikanrong@gmail.com>
*/

#include <config.h>
#include "fm-ogl-view.h"
#include "fm-ogl-model.h"
#include "fm-ogl-cairo-rendering.h"
#include <string.h>
#include "fm-error-reporting.h"
#include <string.h>
#include <eel/eel-vfs-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <gdk/gdkcursor.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkbindings.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>

/////INCLUDE GTKGLEXT LIB HEADERS/////
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkgl.h>
#include <GL/glut.h>


#include <libegg/eggtreemultidnd.h>
#include <glib/gi18n.h>
//#include <libgnome/gnome-macros.h>
#include <libnautilus-extension/nautilus-column-provider.h>
#include <libnautilus-private/nautilus-column-chooser.h>
#include <libnautilus-private/nautilus-column-utilities.h>
#include <libnautilus-private/nautilus-debug-log.h>
#include <libnautilus-private/nautilus-directory-background.h>
#include <libnautilus-private/nautilus-dnd.h>
#include <libnautilus-private/nautilus-file.h>
#include <libnautilus-private/nautilus-file-private.h>
#include <libnautilus-private/nautilus-file-dnd.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-ui-utilities.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-icon-dnd.h>
#include <libnautilus-private/nautilus-metadata.h>
#include <libnautilus-private/nautilus-module.h>
#include <libnautilus-private/nautilus-tree-view-drag-dest.h>
#include <libnautilus-private/nautilus-view-factory.h>
#include <libnautilus-private/nautilus-clipboard.h>
#include <libnautilus-private/nautilus-cell-renderer-pixbuf-emblem.h>
#include <libnautilus-private/nautilus-cell-renderer-text-ellipsized.h>

static GdkGLContext* gl_shared_context = NULL;

static void   fm_ogl_view_iface_init                      	(NautilusViewIface *iface);
static GList *fm_ogl_view_get_selection                   	(FMDirectoryView   *view);
static void  fm_ogl_view_scrollbar_change					(GtkWidget* pWidget);
static void fm_ogl_view_set_proper_scrollbar_adjustment		(FMOGLView *ogl_view);

G_DEFINE_TYPE_WITH_CODE (FMOGLView, fm_ogl_view, FM_TYPE_DIRECTORY_VIEW, 
			 G_IMPLEMENT_INTERFACE (NAUTILUS_TYPE_VIEW,
						fm_ogl_view_iface_init));

#define parent_class fm_ogl_view_parent_class

static const GtkActionEntry ogl_view_entries[] = {

};

struct FMOGLViewDetails {
	GtkActionGroup *ogl_action_group;
	guint ogl_merge_id;
	GtkDrawingArea *drawing_area;
	GtkHScrollbar *scrollbar;
	GtkHBox *box;
	GtkAdjustment *adjustment;
	gboolean menus_ready;
	FMOGLModel *model;
	NautilusZoomLevel zoom_level;
	guint ogl_drawing_area_idle_id;
	GTimer* pTimerId;
	gulong	 ulMilliSeconds;
	guint uiDrawHandlerId;
	GSequence *gsFileOglDetails;
	gdouble scroll_position_target;
	gdouble scroll_position_current;
	gdouble zoom_position_current;
	gdouble file_bounding_box[4];
	gdouble file_side_length;
};

static const char *
fm_ogl_view_get_id (NautilusView *view)
{
	return FM_OGL_VIEW_ID;
}

static char *
fm_ogl_view_get_first_visible_file (NautilusView *view)
{
	return NULL;
}

static void
ogl_view_scroll_to_file (NautilusView *view,
			  const char *uri)
{

}

static gboolean
fm_ogl_view_supports_uri (const char *uri,
			   GFileType file_type,
			   const char *mime_type)
{
	return TRUE;
}

void 
fm_ogl_clear_file_details(FMOGLView* view)
{
		GSequence *file_details = view->details->gsFileOglDetails;
	 	/*** OpenGL BEGIN ***/
			
			GSequenceIter *iter = g_sequence_get_begin_iter(file_details);
	
			while( iter != g_sequence_get_end_iter(file_details)){	
				GSequenceIter *iter_next = g_sequence_iter_next(iter);
				FileEntryOglDetails *file_entry_ogl;
				file_entry_ogl = g_sequence_get (iter);
				g_sequence_remove (iter);
				iter = iter_next;
			}

}

static void
fm_ogl_view_clear (FMDirectoryView *view)
{
	FMOGLView *ogl_view;
	ogl_view = FM_OGL_VIEW (view);
	GtkWidget *widget;
	if (ogl_view->details->drawing_area != NULL){ 
		widget = GTK_WIDGET(ogl_view->details->drawing_area);
	}

	fm_ogl_clear_file_details(ogl_view);

	fm_ogl_model_clear (ogl_view->details->model);
}

static GtkWidget *
fm_ogl_view_get_background_widget (FMDirectoryView *view)
{
	return GTK_WIDGET (view);
}

static gboolean
fm_ogl_view_is_empty (FMDirectoryView *view)
{
	return TRUE;
}

static void
fm_ogl_view_get_selection_foreach_func (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{

}

static void
fm_ogl_view_set_zoom_level (FMOGLView *view,
			     NautilusZoomLevel new_level,
			     gboolean always_emit)
{

	g_return_if_fail (FM_IS_OGL_VIEW (view));
	g_return_if_fail (new_level >= NAUTILUS_ZOOM_LEVEL_SMALLEST &&
			  new_level <= NAUTILUS_ZOOM_LEVEL_LARGEST);

	//todo: put actual zooming code here.
	view->details->zoom_level = new_level;
	g_signal_emit_by_name (FM_DIRECTORY_VIEW(view), "zoom_level_changed");	
	
}

static void
fm_ogl_view_zoom_to_level (FMDirectoryView *view,
			    NautilusZoomLevel zoom_level)
{
	FMOGLView *ogl_view;

	g_return_if_fail (FM_IS_OGL_VIEW (view));

	ogl_view = FM_OGL_VIEW (view);

	fm_ogl_view_set_zoom_level (ogl_view, zoom_level, FALSE);
}

static void
fm_ogl_view_restore_default_zoom_level (FMDirectoryView *view)
{
	FMOGLView *ogl_view;

	g_return_if_fail (FM_IS_OGL_VIEW (view));

	ogl_view = FM_OGL_VIEW (view);
	ogl_view->details->zoom_position_current = (gdouble)(NAUTILUS_ZOOM_LEVEL_SMALLEST+1);
	fm_ogl_view_set_zoom_level (ogl_view, NAUTILUS_ZOOM_LEVEL_SMALLEST+1, TRUE);
}

static void
fm_ogl_view_bump_zoom_level (FMDirectoryView *view, int zoom_increment)
{
	FMOGLView *ogl_view;
	NautilusZoomLevel new_level;

	g_return_if_fail (FM_IS_OGL_VIEW (view));

	ogl_view = FM_OGL_VIEW (view);
	new_level = ogl_view->details->zoom_level + zoom_increment;

	if (new_level >= NAUTILUS_ZOOM_LEVEL_SMALLEST &&
	    new_level <= NAUTILUS_ZOOM_LEVEL_LARGEST) {
		fm_ogl_view_set_zoom_level (ogl_view, new_level, FALSE);
	}
}

static NautilusZoomLevel
fm_ogl_view_get_zoom_level (FMDirectoryView *view)
{
	FMOGLView *ogl_view;

	g_return_val_if_fail (FM_IS_OGL_VIEW (view), NAUTILUS_ZOOM_LEVEL_STANDARD);

	ogl_view = FM_OGL_VIEW (view);

	return ogl_view->details->zoom_level;
}


static gboolean 
fm_ogl_view_can_zoom_in (FMDirectoryView *view) 
{
	g_return_val_if_fail (FM_IS_OGL_VIEW (view), FALSE);

	return FM_OGL_VIEW (view)->details->zoom_level	< NAUTILUS_ZOOM_LEVEL_LARGEST;
}

static gboolean 
fm_ogl_view_can_zoom_out (FMDirectoryView *view) 
{
	g_return_val_if_fail (FM_IS_OGL_VIEW (view), FALSE);

	return FM_OGL_VIEW (view)->details->zoom_level	> NAUTILUS_ZOOM_LEVEL_SMALLEST;
}

static GList *
fm_ogl_view_get_selection (FMDirectoryView *view)
{
	return NULL;
}

static int
fm_ogl_view_compare_files (FMDirectoryView *view, NautilusFile *file1, NautilusFile *file2)
{
	return 1;
}
static void
fm_ogl_view_add_file (FMDirectoryView *view, NautilusFile *file, NautilusDirectory *directory)
{
	FMOGLModel *model;
	model = FM_OGL_VIEW (view)->details->model;
	printf("FILE ADDED TO OGL VIEW: %s (Files Length: %d)\n", file->details->name, g_sequence_get_length(model->details->files));
	fm_ogl_model_add_file (model, file, directory);
}

static void
fm_ogl_view_file_changed (FMDirectoryView *view, NautilusFile *file, NautilusDirectory *directory)
{

}

static guint
fm_ogl_view_get_item_count (FMDirectoryView *view)
{
	g_return_val_if_fail (FM_IS_OGL_VIEW (view), 0);

	return fm_ogl_model_get_length (FM_OGL_VIEW (view)->details->model);
}

static void
fm_ogl_view_iface_init (NautilusViewIface *iface)
{
	fm_directory_view_init_view_iface (iface);
	iface->get_view_id = fm_ogl_view_get_id;
	iface->get_first_visible_file = fm_ogl_view_get_first_visible_file;
	iface->scroll_to_file = ogl_view_scroll_to_file;
}
static void
fm_ogl_view_end_loading(FMDirectoryView *view)
{
	 FMOGLModel *model;
	 model = FM_OGL_VIEW (view)->details->model;
	 
	 GtkWidget *widget = GTK_WIDGET(FM_OGL_VIEW (view)->details->drawing_area);
	 GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  	 GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);

  	 gdk_gl_drawable_gl_begin (gldrawable, glcontext);
	 GSequence *files = model->details->files;
	 GSequence *file_details = FM_OGL_VIEW (view)->details->gsFileOglDetails;
	 
	 if (g_sequence_get_length (files) >= 1) {
	 	int i=0;
	 	for(i=0; i < g_sequence_get_length (files); i++){
	 		GSequenceIter *file_ptr = g_sequence_get_iter_at_pos (files, i);
	 		FileEntry *file = g_sequence_get (file_ptr);
	 		FileEntryOglDetails *details = g_new0 (FileEntryOglDetails, 1);
	 		 
	 		fm_ogl_cairo_render_create_context(&details->pTextureData, &details->pCairoSurface, &details->pCairoContext);
	 		
			int icon_size = nautilus_get_icon_size_for_zoom_level (NAUTILUS_ZOOM_LEVEL_LARGEST);
			NautilusFileIconFlags flags = NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS | NAUTILUS_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE;
			GdkPixbuf *icon = nautilus_file_get_icon_pixbuf (file->file, icon_size, TRUE, flags);

	 		fm_ogl_cairo_render_file_entry(&details->pCairoContext, file->file->details->name, icon);
	 		fm_ogl_cairo_create_ogl_texture(details);
	 		details->file_entry = file;
	 		details->ptr = g_sequence_insert_sorted (file_details, details, fm_ogl_cairo_file_entry_compare_func, NULL);

	 	}
	 }
	 gdk_gl_drawable_gl_end (gldrawable);
	 
	 fm_ogl_view_set_proper_scrollbar_adjustment(FM_OGL_VIEW (view));
	 
}

static void
fm_ogl_view_set_proper_scrollbar_adjustment(FMOGLView *ogl_view){
	 FMOGLModel *model = ogl_view->details->model;
	 gint files_count = g_sequence_get_length(model->details->files);
	 gint total_block_width = (files_count/3)+1;
	 gdouble zoom = ogl_view->details->zoom_position_current;
	 gdouble block_pixels = ogl_view->details->file_side_length;
	 gfloat total_width = GTK_WIDGET(ogl_view->details->drawing_area)->allocation.width;
	 gfloat inner_width = (total_width - (ogl_view->details->file_bounding_box[2] - ogl_view->details->file_bounding_box[0]));
	 GtkAdjustment* adj = ogl_view->details->adjustment;

	 gtk_adjustment_set_upper(adj, total_block_width*1.1f);
	 gtk_adjustment_set_page_size(adj, 1.1);
	 
	 gtk_adjustment_set_step_increment(adj,	.1);
	 gtk_adjustment_set_page_increment(adj, 1.1);
	 
	 gtk_range_set_adjustment(ogl_view->details->scrollbar, ogl_view->details->adjustment);
}

static void
fm_ogl_view_begin_loading(FMDirectoryView *view)
{
	FMOGLView* ogl_view = FM_OGL_VIEW(view);
	
	fm_ogl_view_restore_default_zoom_level(view);
}

static void 
fm_ogl_view_scrollbar_change(GtkWidget* pWidget){
  GtkHScrollbar *scrollbar = GTK_HSCROLLBAR(pWidget);
  scrollbar = scrollbar;
  double increment = gtk_range_get_value(GTK_RANGE(scrollbar));
  FMOGLView *view = FM_OGL_VIEW(pWidget->parent->parent);
  view->details->scroll_position_target = increment;
  
}

static void
fm_ogl_view_dispose (GObject *object)
{
	FMOGLView *ogl_view;
	GtkUIManager *ui_manager;

	ogl_view = FM_OGL_VIEW (object);

	g_source_remove (ogl_view->details->uiDrawHandlerId);

	G_OBJECT_CLASS (fm_ogl_view_parent_class)->dispose (object);
}

static void
fm_ogl_view_finalize (GObject *object)
{
	FMOGLView *ogl_view;

	ogl_view = FM_OGL_VIEW (object);

	if (ogl_view->details->drawing_area != NULL && GTK_IS_WIDGET(ogl_view->details->drawing_area)) {
		gtk_widget_destroy (GTK_WIDGET(ogl_view->details->drawing_area));
	}

	g_free (ogl_view->details);


	G_OBJECT_CLASS (fm_ogl_view_parent_class)->finalize (object);
}

static void
ogl_drawing_area_realize_event_callback(GtkWidget *widget, gpointer data)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);
	if (gl_shared_context == NULL){
		gl_shared_context = glcontext;
	}
  /*** OpenGL BEGIN ***/
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
    return;
  
  glClearColor(0.9,0.9,0.9,1.0);
  glClearDepth(1.0);
  
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
  glDisable (GL_CULL_FACE);

  //Lighting
  glEnable (GL_LIGHTING);
  glEnable (GL_LIGHT0);
  GLfloat afLightDiffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
  GLfloat afLightAmbient[] = {0.2f, 0.2f, 0.2f, 1.0f};
  glLightModelf (GL_LIGHT_MODEL_TWO_SIDE, 1.0f);
  glLightModelfv (GL_LIGHT_MODEL_AMBIENT, afLightAmbient);
  glLightfv (GL_LIGHT0, GL_DIFFUSE, afLightDiffuse);
  

  glEnable (GL_NORMALIZE);
  
  glEnable (GL_TEXTURE_2D);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			 GL_TEXTURE_MIN_FILTER,
			 GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			 GL_TEXTURE_MAG_FILTER,
			 GL_LINEAR);

			 
  gdk_gl_drawable_gl_end (gldrawable);
}

gboolean
ogl_drawing_area_draw_handler (GtkWidget* pWidget)
{
	gtk_widget_queue_draw (pWidget);

	return TRUE;
}

gboolean
ogl_drawing_area_expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  FMOGLView *view = (FMOGLView*)data;
  FMOGLModel *model;
  model = FM_OGL_VIEW (view)->details->model;
  GSequence *file_details = view->details->gsFileOglDetails;
  static gint iFrames = 0;
  static gdouble fLastTimeStamp = 0.0f;
  static gdouble fCurrentTimeStamp = 0.0f;
  static gdouble fLastFullSecond = 0.0f;
  gdouble fFrameTimeDelta = 0.0f;
  gdouble fFullSecond = 0.0f;
	 
  GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);

  /*** OpenGL BEGIN ***/
  gdk_gl_drawable_gl_begin (gldrawable, glcontext);
    gdk_gl_drawable_swap_buffers (gldrawable);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		if(view->details->scroll_position_target != view->details->scroll_position_current)
			fm_ogl_cairo_do_gradient(view->details->scroll_position_target, &view->details->scroll_position_current, 13.0, 0.0001);
		
		if(view->details->zoom_position_current != (gdouble)view->details->zoom_level){
			fm_ogl_cairo_do_gradient((gdouble)view->details->zoom_level, &view->details->zoom_position_current, 20.0, 0.001);
			fm_ogl_view_set_proper_scrollbar_adjustment(view);
		}
    	
    	int i=0;
	 	for(i=0; i < g_sequence_get_length (file_details); i++){
	 		GSequenceIter *file_ptr = g_sequence_get_iter_at_pos (file_details, i);
		 	FileEntryOglDetails *file_details = g_sequence_get (file_ptr);
		 	fm_ogl_cairo_draw_file(i, &file_details->auiColorBuffer, view->details->scroll_position_current, view->details->zoom_position_current, view->details->file_bounding_box, &view->details->file_side_length);
		}
	
		gdouble block_pixels = view->details->file_side_length;
	 	gfloat total_width = GTK_WIDGET(view->details->drawing_area)->allocation.width;
	 	gfloat total_height = GTK_WIDGET(view->details->drawing_area)->allocation.height;
	 	
		fm_ogl_cairo_render_hud(block_pixels, total_width, total_height, view->details->file_bounding_box);
		
	
  gdk_gl_drawable_gl_end (gldrawable);
  
  	GTimer* f_pTimerId = FM_OGL_VIEW (view)->details->pTimerId;
	gulong	 f_ulMilliSeconds;
	
  	fCurrentTimeStamp = g_timer_elapsed (f_pTimerId, &f_ulMilliSeconds);
	f_ulMilliSeconds /= 1000;
	fFrameTimeDelta = fCurrentTimeStamp - fLastTimeStamp;
	fFullSecond = fCurrentTimeStamp - fLastFullSecond;

	if (fFullSecond < 1.0f)
		iFrames++;
	else
	{
		g_print ("fps: %d, last frame-time: %f\n",
			 iFrames,
			 fFrameTimeDelta);
		iFrames = 0;
		fLastFullSecond = fCurrentTimeStamp;
	}

	fLastTimeStamp = fCurrentTimeStamp;
	

  return TRUE;
}


static gboolean
ogl_drawing_area_configure_event_callback (GtkWidget         *widget,
	 GdkEventConfigure *event,
	 gpointer           data)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);

  GLfloat h = (GLfloat) (widget->allocation.height) / (GLfloat) (widget->allocation.width);

  /*** OpenGL BEGIN ***/
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
    return FALSE;
	  if(FM_OGL_VIEW_ANTIALIAS){
		  //Anti-aliasing
	 	  glEnable(GL_POLYGON_SMOOTH);
	  }
	  glViewport (0, 0, widget->allocation.width, widget->allocation.height);
	  glMatrixMode (GL_PROJECTION);
	  glLoadIdentity ();
	  glFrustum (-1.0, 1.0, -h, h, 5, 50);
	  glMatrixMode (GL_MODELVIEW);
	  glLoadIdentity ();
	  glDisable( GL_DITHER );
	  glEnable (GL_BLEND);
	  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          
  gdk_gl_drawable_gl_end (gldrawable);
  /*** OpenGL END ***/

  return TRUE;
}



void
ogl_drawing_area_init(GtkWidget *widget, FMOGLView *ogl_view)
{
	

	GdkGLConfig *glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB   |
					       GDK_GL_MODE_ALPHA |
					       GDK_GL_MODE_DEPTH |
					       GDK_GL_MODE_DOUBLE);
					       
	gtk_widget_set_gl_capability (widget,
				glconfig,
				gl_shared_context,
				TRUE,
				GDK_GL_RGBA_TYPE);

	gtk_widget_add_events (widget,
			 GDK_VISIBILITY_NOTIFY_MASK);
			 			
	g_signal_connect (G_OBJECT (widget), "expose_event",  
    		G_CALLBACK (ogl_drawing_area_expose_event_callback), ogl_view);

		    
	g_signal_connect (G_OBJECT (widget), "configure_event",
		    G_CALLBACK (ogl_drawing_area_configure_event_callback), NULL);
		    

		    
    g_signal_connect_after (G_OBJECT (widget), "realize",
            G_CALLBACK (ogl_drawing_area_realize_event_callback), NULL);
    
	ogl_view->details->uiDrawHandlerId = g_timeout_add (30,
					 (GSourceFunc) ogl_drawing_area_draw_handler,
					 ogl_view);
	
	gtk_container_add (GTK_CONTAINER (ogl_view->details->box), widget);
	
	gtk_widget_show (widget);
}

static void
fm_ogl_view_merge_menus (FMDirectoryView *view)
{
	FMOGLView *ogl_view;
	GtkUIManager *ui_manager;
	GtkActionGroup *action_group;
	const char *ui;

	EEL_CALL_PARENT (FM_DIRECTORY_VIEW_CLASS, merge_menus, (view));

	ogl_view = FM_OGL_VIEW (view);

	ui_manager = fm_directory_view_get_ui_manager (view);

	action_group = gtk_action_group_new ("OGLViewActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	ogl_view->details->ogl_action_group = action_group;
	gtk_action_group_add_actions (action_group, 
				      ogl_view_entries, G_N_ELEMENTS (ogl_view_entries),
				      ogl_view);

	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
	g_object_unref (action_group); /* owned by ui manager */

	ui = nautilus_ui_string_get ("nautilus-ogl-view-ui.xml");
	ogl_view->details->ogl_merge_id = gtk_ui_manager_add_ui_from_string (ui_manager, ui, -1, NULL);

	ogl_view->details->menus_ready = TRUE;
}

static void
fm_ogl_view_unmerge_menus (FMDirectoryView *view)
{
	FMOGLView *ogl_view;
	GtkUIManager *ui_manager;

	ogl_view = FM_OGL_VIEW (view);

	FM_DIRECTORY_VIEW_CLASS (fm_ogl_view_parent_class)->unmerge_menus (view);

	ui_manager = fm_directory_view_get_ui_manager (view);
	if (ui_manager != NULL) {
		nautilus_ui_unmerge_ui (ui_manager,
					&ogl_view->details->ogl_merge_id,
					&ogl_view->details->ogl_action_group);
	}
}

static void
fm_ogl_view_init (FMOGLView *ogl_view)
{
	gdk_threads_init();
	
	ogl_view->details = g_new0 (FMOGLViewDetails, 1);
	ogl_view->details->box = gtk_vbox_new(FALSE, 0);
	gtk_container_add (GTK_CONTAINER (ogl_view), GTK_WIDGET(ogl_view->details->box));
	gtk_widget_show (GTK_WIDGET(ogl_view->details->box));
	
	ogl_view->details->scrollbar = GTK_HSCROLLBAR(gtk_hscrollbar_new(ogl_view->details->adjustment));
	g_signal_connect (G_OBJECT (ogl_view->details->scrollbar), "value_changed", G_CALLBACK (fm_ogl_view_scrollbar_change), NULL);
	gtk_container_add (GTK_CONTAINER (ogl_view->details->box), GTK_WIDGET(ogl_view->details->scrollbar));
	
	gtk_widget_show (GTK_WIDGET(ogl_view->details->scrollbar));
	gtk_box_set_child_packing (ogl_view->details->box, GTK_WIDGET(ogl_view->details->scrollbar), FALSE, FALSE, 0, GTK_PACK_END);
                                                         
	ogl_view->details->zoom_level = NAUTILUS_ZOOM_LEVEL_SMALLEST - 1;
	ogl_view->details->drawing_area = GTK_DRAWING_AREA (gtk_drawing_area_new ());
	ogl_view->details->model = g_object_new (FM_TYPE_OGL_MODEL, NULL);
	ogl_view->details->pTimerId = g_timer_new ();
	ogl_view->details->ulMilliSeconds = 0L;
	ogl_view->details->gsFileOglDetails = g_sequence_new ((GDestroyNotify)fm_ogl_cairo_destroy_file_ogl_details);
	ogl_view->details->scroll_position_target = 0.0;
	ogl_view->details->scroll_position_current = 0.0;
	ogl_drawing_area_init(GTK_WIDGET (ogl_view->details->drawing_area), ogl_view);

	
	
	ogl_view->details->adjustment = gtk_adjustment_new(0,0,0,0,0,0);

}

static void
fm_ogl_view_update_menus (FMDirectoryView *view)
{
	FMOGLView *ogl_view;

        ogl_view = FM_OGL_VIEW (view);

	/* don't update if the menus aren't ready */
	if (!ogl_view->details->menus_ready) {
		return;
	}

	EEL_CALL_PARENT (FM_DIRECTORY_VIEW_CLASS, update_menus, (view));
}


static void
fm_ogl_view_class_init (FMOGLViewClass *class)
{
	FMDirectoryViewClass *fm_directory_view_class;

	fm_directory_view_class = FM_DIRECTORY_VIEW_CLASS (class);
	
	G_OBJECT_CLASS (class)->dispose = fm_ogl_view_dispose;
	G_OBJECT_CLASS (class)->finalize = fm_ogl_view_finalize;
	
	fm_directory_view_class->get_background_widget = fm_ogl_view_get_background_widget;
	fm_directory_view_class->is_empty = fm_ogl_view_is_empty;
	fm_directory_view_class->get_selection = fm_ogl_view_get_selection;
	fm_directory_view_class->clear = fm_ogl_view_clear;
	fm_directory_view_class->bump_zoom_level = fm_ogl_view_bump_zoom_level;
	fm_directory_view_class->get_zoom_level = fm_ogl_view_get_zoom_level;
	fm_directory_view_class->can_zoom_out = fm_ogl_view_can_zoom_out;
	fm_directory_view_class->can_zoom_in = fm_ogl_view_can_zoom_in;
	fm_directory_view_class->zoom_to_level = fm_ogl_view_zoom_to_level;
	fm_directory_view_class->restore_default_zoom_level = fm_ogl_view_restore_default_zoom_level;
	fm_directory_view_class->compare_files = fm_ogl_view_compare_files;
	fm_directory_view_class->add_file = fm_ogl_view_add_file;
	fm_directory_view_class->merge_menus = fm_ogl_view_merge_menus;
	fm_directory_view_class->unmerge_menus = fm_ogl_view_unmerge_menus;
	fm_directory_view_class->update_menus = fm_ogl_view_update_menus;
	fm_directory_view_class->file_changed = fm_ogl_view_file_changed;
	fm_directory_view_class->get_item_count = fm_ogl_view_get_item_count;
	fm_directory_view_class->end_loading = fm_ogl_view_end_loading;
	fm_directory_view_class->begin_loading = fm_ogl_view_begin_loading;
}

static NautilusView *
fm_ogl_view_create (NautilusWindowSlotInfo *slot)
{
	FMOGLView *view;

	view = g_object_new (FM_TYPE_OGL_VIEW, "window-slot", slot, NULL);
	g_object_ref (view);
	gtk_object_sink (GTK_OBJECT (view));
	return NAUTILUS_VIEW (view);
}

static NautilusViewInfo fm_ogl_view = {
	FM_OGL_VIEW_ID,
	N_("Effects"),
	N_("_Effects"),
	N_("The effect view encountered an error."),
	N_("The effect view encountered an error while starting up."),
	N_("Display this location with the effect view."),
	fm_ogl_view_create,
	fm_ogl_view_supports_uri
};

void
fm_ogl_view_register (void)
{
	fm_ogl_view.view_combo_label = _(fm_ogl_view.view_combo_label);
	fm_ogl_view.view_menu_label_with_mnemonic = _(fm_ogl_view.view_menu_label_with_mnemonic);
	fm_ogl_view.error_label = _(fm_ogl_view.error_label);
	fm_ogl_view.startup_error_label = _(fm_ogl_view.startup_error_label);
	fm_ogl_view.display_location_label = _(fm_ogl_view.display_location_label);

	nautilus_view_factory_register (&fm_ogl_view);
}
