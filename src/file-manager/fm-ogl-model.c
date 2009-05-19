#include <config.h>
#include "fm-ogl-model.h"
#include <libegg/eggtreemultidnd.h>

#include <string.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gdk-pixbuf-extensions.h>
#include <gtk/gtktreednd.h>
#include <gtk/gtktreesortable.h>
#include <glib/gi18n.h>
#include <libnautilus-private/nautilus-dnd.h>

#if GLIB_CHECK_VERSION (2, 13, 0)
#include <glib/gsequence.h>
#else
#include <gsequence/gsequence.h>
#endif

static GObjectClass *parent_class;

static void
file_entry_free (FileEntry *file_entry)
{
	nautilus_file_unref (file_entry->file);
	if (file_entry->reverse_map) {
		g_hash_table_destroy (file_entry->reverse_map);
		file_entry->reverse_map = NULL;
	}
	if (file_entry->subdirectory != NULL) {
		nautilus_directory_unref (file_entry->subdirectory);
	}
	if (file_entry->files != NULL) {
		g_sequence_free (file_entry->files);
	}

	g_free (file_entry);
}

static int
fm_ogl_model_file_entry_compare_func (gconstpointer a,
				       gconstpointer b,
				       gpointer      user_data)
{
	FileEntry *file_entry1;
	FileEntry *file_entry2;
	FMOGLModel *model;
	int result;

	model = (FMOGLModel *)user_data;

	file_entry1 = (FileEntry *)a;
	file_entry2 = (FileEntry *)b;
	
	if (file_entry1->file != NULL && file_entry2->file != NULL) {
		result = nautilus_file_compare_for_sort_by_attribute_q (file_entry1->file, file_entry2->file,
									model->details->sort_attribute,
									model->details->sort_directories_first,
									(model->details->order == GTK_SORT_DESCENDING));
	} else if (file_entry1->file == NULL) {
		return -1;
	} else {
		return 1;
	}

	return result;
}

guint
fm_ogl_model_get_length (FMOGLModel *model)
{
	return g_sequence_get_length (model->details->files);
}

gboolean
fm_ogl_model_add_file (FMOGLModel *model, NautilusFile *file,
			NautilusDirectory *directory)
{

	FileEntry *file_entry;
	GSequence *files;

	
	file_entry = g_new0 (FileEntry, 1);
	file_entry->file = nautilus_file_ref (file);
	file_entry->parent = NULL;
	file_entry->subdirectory = NULL;
	file_entry->files = NULL;
	
	files = model->details->files;

	
	file_entry->ptr = g_sequence_insert_sorted (files, file_entry,
					    fm_ogl_model_file_entry_compare_func, model);

	
	return TRUE;
}

static void
fm_ogl_model_remove (FMOGLModel *model, GSequenceIter *iter)
{
	FileEntry *file_entry;
	file_entry = g_sequence_get (iter);
	g_sequence_remove (iter);
}


static void
fm_ogl_model_clear_directory (FMOGLModel *model, GSequence *files)
{
	GSequenceIter *iter = g_sequence_get_begin_iter(files);
	
	while( iter != g_sequence_get_end_iter(files)){	
		GSequenceIter *iter_next = g_sequence_iter_next(iter);
		fm_ogl_model_remove(model, iter);
		iter = iter_next;
	}
}

void
fm_ogl_model_clear (FMOGLModel *model)
{
	g_return_if_fail (model != NULL);

	fm_ogl_model_clear_directory (model, model->details->files);
}

static void
fm_ogl_model_dispose (GObject *object)
{
	FMOGLModel *model;
	int i;

	model = FM_OGL_MODEL (object);

	if (model->details->files) {
		g_sequence_free (model->details->files);
		model->details->files = NULL;
	}
	
	if (model->details->top_reverse_map) {
		g_hash_table_destroy (model->details->top_reverse_map);
		model->details->top_reverse_map = NULL;
	}
	if (model->details->directory_reverse_map) {
		g_hash_table_destroy (model->details->directory_reverse_map);
		model->details->directory_reverse_map = NULL;
	}
	
	EEL_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
fm_ogl_model_finalize (GObject *object)
{
	FMOGLModel *model;

	model = FM_OGL_MODEL (object);

	g_free (model->details);
	
	EEL_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
fm_ogl_model_init (FMOGLModel *model)
{
	model->details = g_new0 (FMOGLModelDetails, 1);
	model->details->files = g_sequence_new ((GDestroyNotify)file_entry_free);
	model->details->top_reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	model->details->directory_reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	model->details->stamp = g_random_int ();
	model->details->sort_attribute = 0;
}

static void
fm_ogl_model_class_init (FMOGLModelClass *klass)
{
	GObjectClass *object_class;
	object_class = (GObjectClass *)klass;
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = fm_ogl_model_finalize;
	object_class->dispose = fm_ogl_model_dispose;
}

GType
fm_ogl_model_get_type (void)
{
	static GType object_type = 0;

	if (object_type == 0) {
		const GTypeInfo object_info = {
			sizeof (FMOGLModelClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) fm_ogl_model_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (FMOGLModel),
			0,
			(GInstanceInitFunc) fm_ogl_model_init,
		};
		object_type = g_type_register_static (G_TYPE_OBJECT, "FMOGLModel", &object_info, 0);
	}
	return object_type;
}
