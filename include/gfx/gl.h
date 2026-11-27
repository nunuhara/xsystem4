/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#ifndef SYSTEM4_GFX_GL_H
#define SYSTEM4_GFX_GL_H

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef USE_GLES
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h>
#elif defined(__APPLE__) && !__has_include(<GLES3/gl3.h>)
#include <OpenGL/gl3.h>
#else
#include <GLES3/gl3.h>
#endif
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#endif /* SYSTEM4_GFX_GL_H */
