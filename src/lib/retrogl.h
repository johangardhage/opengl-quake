//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#ifndef _RETROGL_H_
#define _RETROGL_H_

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#include <stdio.h> // printf
#include <stdlib.h> // exit

// Global OpenGL context
SDL_GLContext glContext = NULL;

// GL 1.2/1.3 tokens missing from some older GL headers
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE 0x8573
#endif

// ARB multitexture entry points, resolved at runtime in RETROGL_Initialize. Older GL
// headers (notably on Linux) do not declare these, so they are loaded via
// SDL_GL_GetProcAddress.
typedef void (*PFN_glActiveTexture)(GLenum texture);
typedef void (*PFN_glMultiTexCoord2f)(GLenum target, GLfloat s, GLfloat t);
PFN_glActiveTexture glActiveTextureFn = NULL;
PFN_glMultiTexCoord2f glMultiTexCoord2fFn = NULL;

void RETROGL_SetAttributes(void)
{
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
}

void RETROGL_UpdateProjection(int width, int height, double fov, double znear, double zfar)
{
	if (width < 1) {
		width = 1;
	}
	if (height < 1) {
		height = 1;
	}

	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(fov, (double)width / (double)height, znear, zfar);
	glMatrixMode(GL_MODELVIEW);
}

void RETROGL_UpdateWindowProjection(SDL_Window *window, int *width, int *height,
		double fov, double znear, double zfar)
{
	SDL_GetWindowSizeInPixels(window, width, height);
	RETROGL_UpdateProjection(*width, *height, fov, znear, zfar);
}

bool RETROGL_Initialize(SDL_Window *window, bool vsync)
{
	// Create the OpenGL context and attach it to the window
	glContext = SDL_GL_CreateContext(window);
	if (glContext == NULL) {
		return false;
	}
	SDL_GL_SetSwapInterval(vsync ? 1 : 0);

	// Resolve the multitexture entry points used for lightmapping. Try the core
	// OpenGL 1.3 names first, then the older ARB extension names.
	glActiveTextureFn = (PFN_glActiveTexture)SDL_GL_GetProcAddress("glActiveTexture");
	if (!glActiveTextureFn) {
		glActiveTextureFn = (PFN_glActiveTexture)SDL_GL_GetProcAddress("glActiveTextureARB");
	}
	glMultiTexCoord2fFn = (PFN_glMultiTexCoord2f)SDL_GL_GetProcAddress("glMultiTexCoord2f");
	if (!glMultiTexCoord2fFn) {
		glMultiTexCoord2fFn = (PFN_glMultiTexCoord2f)SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
	}

	// Setup OpenGL render state
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);	// Black background
	glDisable(GL_LIGHTING);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glDrawBuffer(GL_BACK);
	glShadeModel(GL_SMOOTH);
	glClearDepth(1.0);
	glDepthFunc(GL_LEQUAL);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	return true;
}

void RETROGL_Deinitialize(void)
{
	if (glContext) {
		SDL_GL_DestroyContext(glContext);
		glContext = NULL;
	}
}

void RETROGL_BeginFrame(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void RETROGL_EndFrame(SDL_Window *window)
{
	SDL_GL_SwapWindow(window);
}

#endif
