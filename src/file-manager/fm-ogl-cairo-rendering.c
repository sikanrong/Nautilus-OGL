#include "fm-ogl-cairo-rendering.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-string.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include <GL/glut.h>

void 
fm_ogl_cairo_render_create_context (guchar **pTextureData, cairo_surface_t **pCairoSurface, cairo_t **pCairoContext)
{
	*pTextureData = g_malloc0 (4 * FM_OGL_MODEL_TEXTURE_WIDTH * FM_OGL_MODEL_TEXTURE_HEIGHT);
	if (*pTextureData)
	{
		*pCairoSurface = cairo_image_surface_create_for_data
					(*pTextureData,
					 CAIRO_FORMAT_ARGB32,
					 FM_OGL_MODEL_TEXTURE_WIDTH,
					 FM_OGL_MODEL_TEXTURE_HEIGHT,
					 4 * FM_OGL_MODEL_TEXTURE_WIDTH);
		*pCairoContext = cairo_create (*pCairoSurface);
	}
}

void 
fm_ogl_cairo_render_hud(gdouble block_pixels, gfloat total_width, gfloat total_height, gdouble* inner_bounding_rect, GSequence* file_entries)
{
    glMatrixMode (GL_PROJECTION);
    glPushMatrix();
	glLoadIdentity();
	
	gdouble inner_left = inner_bounding_rect[0];
	gdouble inner_right = inner_bounding_rect[2];
        
	glOrtho(0.0f,total_width,total_height,0.0f,-1.0f,1.0f);
    
	glMatrixMode (GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
	
        glDisable (GL_TEXTURE_RECTANGLE_ARB);
	glDisable (GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable( GL_LIGHTING );
	glPushAttrib( GL_DEPTH_BUFFER_BIT | GL_LIGHTING_BIT );

    //DO DRAWING HERE

    //draw screen
    glBegin(GL_LINES);

    gfloat hud_reduction_factor = 6.0;
    gfloat screen_width = (total_width*(1/hud_reduction_factor));
    gfloat screen_height = (total_height*(1/hud_reduction_factor));
    gfloat hud_pixels_from_bottom = screen_height+15;

    gfloat screen_left = ((total_width/2)-(screen_width/2));
    gfloat screen_top = (total_height-hud_pixels_from_bottom);
    gfloat screen_right = screen_left+screen_width;
    gfloat screen_bottom = screen_top+screen_height;

	glColor3f(0.0f,0.0f,0.0f);
	glVertex2f(screen_left, screen_top); glVertex2f(screen_right, screen_top); //top
	glVertex2f(screen_right, screen_top); glVertex2f(screen_right, screen_bottom); //right
	glVertex2f(screen_right, screen_bottom); glVertex2f(screen_left, screen_bottom); //bottom
	glVertex2f(screen_left, screen_bottom); glVertex2f(screen_left, screen_top); //left

	glEnd();

	glBegin(GL_QUADS);
	gint file_entries_length = g_sequence_get_length(file_entries);
	int entry_i;
	for(entry_i = 0; entry_i < file_entries_length; entry_i++){
		gfloat row = entry_i%3;
		gfloat col = entry_i/3;
		gfloat total_hud_width = (inner_right - inner_left)*(1/hud_reduction_factor);
		gfloat total_hud_block_size = block_pixels*(1/hud_reduction_factor);
		gfloat total_hud_padding = block_pixels*(1/hud_reduction_factor)*0.1;

		gfloat entry_left = (screen_left)+(((inner_left)*(1/hud_reduction_factor))+(col*(total_hud_padding+total_hud_block_size)));
		gfloat entry_top = (screen_top+((screen_height/2)-((total_hud_block_size+total_hud_padding)*1.5)))+(row*(total_hud_padding+total_hud_block_size));
		gfloat entry_right = entry_left+total_hud_block_size;
		gfloat entry_bottom = entry_top+total_hud_block_size;

		glVertex2f(entry_left, entry_top);
		glVertex2f(entry_right, entry_top);
		glVertex2f(entry_right, entry_bottom);
		glVertex2f(entry_left, entry_bottom);

	}
	glEnd();

	glColor3f(1.0f,1.0f,1.0f);

        glMatrixMode (GL_PROJECTION);
        glPopMatrix();

        glMatrixMode (GL_MODELVIEW);
        glPopMatrix();
        glEnable (GL_TEXTURE_RECTANGLE_ARB);
}

void 
fm_ogl_cairo_render_file_entry (cairo_t **pCairoContext, eel_ref_str filename, GdkPixbuf *icon_data)
{
  PangoLayout *layout;
  PangoFontDescription *desc;
  
  cairo_set_operator (*pCairoContext, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (*pCairoContext, 1.0, 1.0, 1.0, 0.0);
  cairo_paint (*pCairoContext);
  
  gdk_cairo_set_source_pixbuf(*pCairoContext,
                            icon_data,
                            0.0,
                            0.0); 
  

  cairo_paint (*pCairoContext);
  

  cairo_translate (*pCairoContext, 10, (FM_OGL_MODEL_TEXTURE_HEIGHT/2)+30);
  
  cairo_save (*pCairoContext);
	  /* Create a PangoLayout, set the font and text */
	  cairo_set_source_rgb (*pCairoContext, 0.0, 0.0, 0.0);
	      
	  layout = pango_cairo_create_layout (*pCairoContext);
	  pango_layout_set_text (layout, filename, -1);
	  desc = pango_font_description_from_string (FM_OGL_MODEL_TEXTURE_FONT);
	  pango_layout_set_font_description (layout, desc);
	  pango_font_description_free (desc);
	  pango_cairo_update_layout (*pCairoContext, layout);
	  pango_cairo_show_layout (*pCairoContext, layout);

  cairo_restore (*pCairoContext);
  g_object_unref (layout);
}

void 
fm_ogl_cairo_create_ogl_texture (FileEntryOglDetails *file_details)
{
	glGenTextures (1, &file_details->auiColorBuffer);
	glBindTexture (GL_TEXTURE_RECTANGLE_ARB, file_details->auiColorBuffer);
  	glTexImage2D (GL_TEXTURE_RECTANGLE_ARB,
		      0,
		      GL_RGBA,
		      FM_OGL_MODEL_TEXTURE_WIDTH,
		      FM_OGL_MODEL_TEXTURE_HEIGHT,
		      0,
		      GL_BGRA,
		      GL_UNSIGNED_BYTE,
		      file_details->pTextureData);//End create cairo ogl texture*/
	
	g_free (file_details->pTextureData);
	cairo_destroy(file_details->pCairoContext);
	cairo_surface_destroy(file_details->pCairoSurface);
}

void 
fm_ogl_cairo_do_gradient(gdouble target, gdouble *current, gdouble delta_devide, gdouble tolerance){
	if (ABS(target-*current) <= tolerance){
		*current = target;
	}else{
		gboolean going_up = (*current < target);
		double multiplier = -1.0;
		if(going_up){multiplier *= -1.0;}
		*current += multiplier*(ABS(target-*current)/delta_devide);
	}
}

void
fm_ogl_cairo_draw_file(int index, GLuint *auiColorBuffer, gdouble scroll_position, gdouble zoom_position, gdouble* bounding_box_rect, gdouble* file_side_length)
{
	gdouble modelview_matrix[16];
	gdouble proj_matrix[16];
	gint viewport[4];
	gdouble top_left[3];
	gdouble bottom_right[3];
		
	
    glGetDoublev(GL_PROJECTION_MATRIX, proj_matrix);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix);
	
	    int grid_x = index/3;
    	int grid_y = index%3;
    	gdouble file_transform_coords[3];
    	file_transform_coords[0] = ((float)grid_x * 1.1)-(scroll_position); 
    	file_transform_coords[1] = ((float)grid_y * -1.1f); 
    	file_transform_coords[2] = zoom_position*7.2; 
 		
 		glLoadIdentity ();
		gboolean result = gluProject(file_transform_coords[0]-0.5, file_transform_coords[1]+0.5, 0.0,
               modelview_matrix,
               proj_matrix,
               viewport,
               &top_left[0],
               &top_left[1],
               &top_left[2]);
               
       	result = gluProject(file_transform_coords[0]+0.5, file_transform_coords[1]-0.5, 0.0,
               modelview_matrix,
               proj_matrix,
               viewport,
               &bottom_right[0],
               &bottom_right[1],
               &bottom_right[2]);
               
               if(index == 0){
			   	*file_side_length = bottom_right[0] - top_left[0];
			   }
               
               if(index == 0 || bottom_right[0] > bounding_box_rect[2]){
               	bounding_box_rect[2] = bottom_right[0];
               }
			   if(index == 0 ){
               	bounding_box_rect[0] = top_left[0];
               }
               
               if(index == 0){
               	bounding_box_rect[1] = top_left[1];
               } 
               if(bottom_right[1] < bounding_box_rect[3] || index == 0){
               	bounding_box_rect[3] = bottom_right[1];
               }
        
              
	    
	    glLoadIdentity ();
	    glTranslatef (0.0, 1.1, -FM_OGL_VIEW_INITIAL_CAMERA_Z); 
	    glTranslatef (0.0, 0.0, file_transform_coords[2]);
    	glShadeModel(GL_SMOOTH);
    	
    	
  		
		GLfloat afFrontDiffuseMat[] = {1.0, 1.0, 1.0, 1.0};
		GLfloat afBackDiffuseMat[] = {1.0, 1.0, 1.0, 1.0};
		
		glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, afFrontDiffuseMat);
		glMaterialfv (GL_BACK, GL_AMBIENT_AND_DIFFUSE, afBackDiffuseMat);
   		
  		glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *auiColorBuffer);
		
		glBegin (GL_QUADS);
		
		glNormal3f (0.0f, 0.0f, -1.0f);
		glTexCoord2f (0.0f, 0.0f);
		//file_transform_coords[0], file_transform_coords[1]
		glVertex3f( file_transform_coords[0]-0.5f, file_transform_coords[1]+0.5f, 0.0f);				// Top Left
		glTexCoord2f ((GLfloat) FM_OGL_MODEL_TEXTURE_WIDTH, 0.0f);
		glVertex3f( file_transform_coords[0]+0.5f, file_transform_coords[1]+0.5f, 0.0f);				// Top Right
		glTexCoord2f ((GLfloat) FM_OGL_MODEL_TEXTURE_WIDTH, (GLfloat) FM_OGL_MODEL_TEXTURE_HEIGHT);
		glVertex3f( file_transform_coords[0]+0.5f, file_transform_coords[1]-0.5f, 0.0f);				// Bottom Right      
		glTexCoord2f (0.0f, (GLfloat) FM_OGL_MODEL_TEXTURE_HEIGHT);
		glVertex3f( file_transform_coords[0]-0.5f, file_transform_coords[1]-0.5f, 0.0f);				// Bottom Left
		
		glEnd();
		
		
		
}

void 
fm_ogl_cairo_destroy_file_ogl_details(FileEntryOglDetails *file_ogl_details)
{
	glDeleteTextures (1, &file_ogl_details->auiColorBuffer);
	g_free(file_ogl_details);
}

int
fm_ogl_cairo_file_entry_compare_func (gconstpointer a,
				       gconstpointer b,
				       gpointer      user_data)
{
	return 0;
}

