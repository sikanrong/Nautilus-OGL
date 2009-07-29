#ifndef FMOGLCAIRORENDERING_H_
#define FMOGLCAIRORENDERING_H_

#include <eel/eel-glib-extensions.h>
#include <eel/eel-string.h>
#include <cairo.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include "fm-ogl-model.h"

#define FM_OGL_MODEL_TEXTURE_WIDTH 300
#define FM_OGL_MODEL_TEXTURE_HEIGHT 300
#define FM_OGL_MODEL_TEXTURE_FONT "Sans Bold 27"
#define FM_OGL_VIEW_INITIAL_CAMERA_Z 50.0

typedef struct FileEntryOglDetails FileEntryOglDetails;
struct FileEntryOglDetails {
	FileEntry *file_entry;
	guchar *pTextureData;
	cairo_t *pCairoContext;
	cairo_surface_t *pCairoSurface;
	GLuint auiColorBuffer;
	GSequenceIter *ptr;
};

void fm_ogl_cairo_destroy_file_ogl_details(FileEntryOglDetails *file_ogl_details);
int fm_ogl_cairo_file_entry_compare_func (gconstpointer a, gconstpointer b, gpointer user_data);

void fm_ogl_cairo_render_create_context (guchar **pTextureData, cairo_surface_t **pCairoSurface, cairo_t **pCairoContext);
void fm_ogl_cairo_render_file_entry (cairo_t **pCairoContext, eel_ref_str filename, GdkPixbuf *icon_data);
void fm_ogl_cairo_create_ogl_texture (FileEntryOglDetails *file_ogl_details);
void fm_ogl_cairo_draw_file(int index, GLuint *auiColorBuffer, gdouble scroll_position, gdouble zoom_position, gdouble* bounding_box_rect, gdouble* file_side_length);
void fm_ogl_cairo_do_gradient(gdouble target, gdouble *current, gdouble delta_devide, gdouble tolerance);
void fm_ogl_cairo_render_hud(gdouble block_pixels, gfloat total_width, gfloat total_height, gdouble* inner_bounding_rect, GSequence* file_entries);

#endif /*FMOGLCAIRORENDERING_H_*/
