#include <gtk/gtktreemodel.h>
#include <gtk/gtktreeview.h>
#include <gdk/gdkdnd.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <libnautilus-private/nautilus-file.h>
#include <libnautilus-private/nautilus-directory.h>
#include <libnautilus-extension/nautilus-column.h>

#ifndef FM_OGL_MODEL_H
#define FM_OGL_MODEL_H

#define FM_TYPE_OGL_MODEL fm_ogl_model_get_type()
#define FM_OGL_MODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), FM_TYPE_OGL_MODEL, FMOGLModel))
#define FM_OGL_MODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FM_TYPE_OGL_MODEL, FMOGLModelClass))
#define FM_IS_OGL_MODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FM_TYPE_OGL_MODEL))
#define FM_IS_OGL_MODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FM_TYPE_OGL_MODEL))
#define FM_OGL_MODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FM_TYPE_OGL_MODEL, FMOGLModelClass))



enum {
	FM_OGL_MODEL_FILE_COLUMN,
	FM_OGL_MODEL_SUBDIRECTORY_COLUMN,
	FM_OGL_MODEL_SMALLEST_ICON_COLUMN,
	FM_OGL_MODEL_SMALLER_ICON_COLUMN,
	FM_OGL_MODEL_SMALL_ICON_COLUMN,
	FM_OGL_MODEL_STANDARD_ICON_COLUMN,
	FM_OGL_MODEL_LARGE_ICON_COLUMN,
	FM_OGL_MODEL_LARGER_ICON_COLUMN,
	FM_OGL_MODEL_LARGEST_ICON_COLUMN,
	FM_OGL_MODEL_SMALLEST_EMBLEM_COLUMN,
	FM_OGL_MODEL_SMALLER_EMBLEM_COLUMN,
	FM_OGL_MODEL_SMALL_EMBLEM_COLUMN,
	FM_OGL_MODEL_STANDARD_EMBLEM_COLUMN,
	FM_OGL_MODEL_LARGE_EMBLEM_COLUMN,
	FM_OGL_MODEL_LARGER_EMBLEM_COLUMN,
	FM_OGL_MODEL_LARGEST_EMBLEM_COLUMN,
	FM_OGL_MODEL_FILE_NAME_IS_EDITABLE_COLUMN,
	FM_OGL_MODEL_NUM_COLUMNS
};

typedef struct FileEntry FileEntry;

struct FileEntry {
	NautilusFile *file;
	GHashTable *reverse_map;	/* map from files to GSequenceIter's */
	NautilusDirectory *subdirectory;
	FileEntry *parent;
	GSequence *files;
	GSequenceIter *ptr;
	guint loaded : 1;
};

typedef struct FMOGLModelDetails FMOGLModelDetails;

struct FMOGLModelDetails {
	GSequence *files;
	GHashTable *directory_reverse_map; /* map from directory to GSequenceIter's */
	GHashTable *top_reverse_map;	   /* map from files in top dir to GSequenceIter's */

	int stamp;

	GQuark sort_attribute;
	GtkSortType order;

	gboolean sort_directories_first;
};

typedef struct FMOGLModel {
	GObject parent_instance;
	FMOGLModelDetails *details;
} FMOGLModel;

typedef struct {
	GObjectClass parent_class;

	void (* subdirectory_unloaded)(FMOGLModel *model,
				       NautilusDirectory *subdirectory);
} FMOGLModelClass;

GType    fm_ogl_model_get_type                          (void);
#endif
