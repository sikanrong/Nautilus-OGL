/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fm-ogl-view.h - interface for OpenGL view of directory.

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2001 Anders Carlsson <andersca@gnu.org>
   
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
#ifndef FM_OGL_VIEW_H
#define FM_OGL_VIEW_H

#include "fm-directory-view.h"


#define FM_TYPE_OGL_VIEW fm_ogl_view_get_type()
#define FM_OGL_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), FM_TYPE_OGL_VIEW, FMOGLView))
#define FM_OGL_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FM_TYPE_OGL_VIEW, FMOGLViewClass))
#define FM_IS_OGL_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FM_TYPE_OGL_VIEW))
#define FM_IS_OGL_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FM_TYPE_OGL_VIEW))
#define FM_OGL_VIEW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FM_TYPE_OGL_VIEW, FMOGLViewClass))
  

#define FM_OGL_VIEW_ID "OAFIID:Nautilus_File_Manager_OGL_View"

#ifdef FM_OGL_VIEW_DISABLE_ANTIALIAS
  #define FM_OGL_VIEW_ANTIALIAS FALSE
#else
  #define FM_OGL_VIEW_ANTIALIAS TRUE
#endif

typedef struct FMOGLViewDetails FMOGLViewDetails;

typedef struct {
	FMDirectoryView parent_instance;
	FMOGLViewDetails *details;
} FMOGLView;

typedef struct {
	FMDirectoryViewClass parent_class;
} FMOGLViewClass;

GType fm_ogl_view_get_type (void);
void  fm_ogl_view_register (void);

#endif 
