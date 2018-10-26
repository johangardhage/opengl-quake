//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#ifndef _RETRO_H_
#define _RETRO_H_

#include <SDL3/SDL.h>
#include <math.h> // M_PI
#include <stdio.h> // printf
#include <stdlib.h> // exit
#include <string.h> // memset
#include "retrogl.h"

// *******************************************************************
// Public dynamic functions (implemented by the demo, all optional)
// *******************************************************************

void __attribute__((weak)) DEMO_Startup(void);          // Configure RETRO before the window is created
void __attribute__((weak)) DEMO_Initialize(void);       // Build the scene (a GL context exists)
void __attribute__((weak)) DEMO_Deinitialize(void);     // Tear the scene down
void __attribute__((weak)) DEMO_Render(double deltatime); // Render one frame
void __attribute__((weak)) DEMO_Input(double deltatime); // Poll input once per frame (before render)

// *******************************************************************
// Public variables
// *******************************************************************

#ifndef RETRO_WIDTH
#define RETRO_WIDTH 640
#endif
#ifndef RETRO_HEIGHT
#define RETRO_HEIGHT 480
#endif

enum { RETRO_MODE_FULLSCREEN, RETRO_MODE_FULLWINDOW, RETRO_MODE_WINDOW };

// Framerate the input speeds are tuned for; scale keyboard input by deltatime
// relative to this so movement is consistent regardless of framerate.
#define RETRO_INPUT_FRAMERATE 60.0

struct {
	int mode;
	char *basename;
	const char *title;
	const char *usagekeys;
	bool vsync;
	bool showcursor;
	bool showfps;
	int fpscap;
	double fov;
	double znear;
	double zfar;
	bool quit;
	SDL_Window *window = NULL;
	int width;
	int height;
	const bool *keystate;             // This frame's keyboard state (held keys)
	bool keypressed[SDL_SCANCODE_COUNT]; // Keys that went down this frame (edges, from events)
	float mousedx;                    // This frame's accumulated relative mouse motion
	float mousedy;
	bool discardmousemotion;          // Discard the first motion after grabbing the mouse
} RETRO = {
	.mode = RETRO_MODE_FULLSCREEN,
	.title = "RETRO",
	.usagekeys = NULL,
	.vsync = true,
	.showcursor = false,
	.showfps = false,
	.fpscap = 0,
	.fov = 45.0,
	.znear = 4.0,
	.zfar = 4000.0,
};

// *******************************************************************
// Public functions
// *******************************************************************

void RETRO_RageQuit(const char *message, const char *error = "")
{
	printf(message, error);
	exit(-1);
}

void RETRO_Initialize(void)
{
	// Initialize SDL
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		RETRO_RageQuit("SDL_Init failed: %s\n", SDL_GetError());
	}

	// Get current display mode
	SDL_DisplayID display = SDL_GetPrimaryDisplay();
	if (display == 0) {
		RETRO_RageQuit("SDL_GetPrimaryDisplay failed: %s\n", SDL_GetError());
	}
	const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(display);
	if (dm == NULL) {
		RETRO_RageQuit("SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
	}

	// Set size of window
	RETRO.width = dm->w;
	RETRO.height = dm->h;
	if (RETRO.mode == RETRO_MODE_WINDOW) {
		RETRO.width = RETRO_WIDTH;
		RETRO.height = RETRO_HEIGHT;
	}

	// Set OpenGL attributes
	RETROGL_SetAttributes();

	// Create window
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
	if (RETRO.mode == RETRO_MODE_FULLWINDOW) {
		flags |= SDL_WINDOW_BORDERLESS;
	}
	RETRO.window = SDL_CreateWindow(RETRO.title, RETRO.width, RETRO.height, flags);
	if (RETRO.window == NULL) {
		RETRO_RageQuit("SDL_CreateWindow failed: %s\n", SDL_GetError());
	}

	// Set fullscreen
	if (RETRO.mode == RETRO_MODE_FULLSCREEN) {
		if (!SDL_SetWindowFullscreenMode(RETRO.window, dm)) {
			RETRO_RageQuit("SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError());
		}
		if (!SDL_SetWindowFullscreen(RETRO.window, true)) {
			RETRO_RageQuit("SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
		}
		SDL_SyncWindow(RETRO.window);
	}

	// Cursor and mouse mode
	if (RETRO.showcursor) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
		if (!SDL_SetWindowRelativeMouseMode(RETRO.window, true)) {
			RETRO_RageQuit("SDL_SetWindowRelativeMouseMode failed: %s\n", SDL_GetError());
		}
		// Throw away the transition delta produced by grabbing the mouse.
		RETRO.discardmousemotion = true;
	}

	// Create the OpenGL context and attach it to the window
	if (!RETROGL_Initialize(RETRO.window, RETRO.vsync)) {
		RETRO_RageQuit("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
	}

	// Setup the viewport and frustum
	RETROGL_UpdateWindowProjection(RETRO.window, &RETRO.width, &RETRO.height,
			RETRO.fov, RETRO.znear, RETRO.zfar);
}

void RETRO_Deinitialize(void)
{
	RETROGL_Deinitialize();
	SDL_DestroyWindow(RETRO.window);
	SDL_Quit();
}

double RETRO_DeltaTime(void)
{
	static unsigned long int now = SDL_GetPerformanceCounter();
	static unsigned long int old = 0;

	old = now;
	now = SDL_GetPerformanceCounter();

	return (double)(now - old) / SDL_GetPerformanceFrequency();
}

void RETRO_Quit(void)
{
	RETRO.quit = true;
}

bool RETRO_QuitRequested(void)
{
	SDL_PumpEvents();
	RETRO.keystate = SDL_GetKeyboardState(NULL);
	if (RETRO.quit) {
		return true;
	} else if (RETRO.keystate[SDL_SCANCODE_ESCAPE]) {
		return true;
	} else if (RETRO.keystate[SDL_SCANCODE_Q]) {
		return true;
	}
	return false;
}

// True while a key is held down this frame.
bool RETRO_KeyState(SDL_Scancode key)
{
	return RETRO.keystate && RETRO.keystate[key];
}

// True only on the frame a key transitions to down (edge, ignores key repeats).
bool RETRO_KeyPressed(SDL_Scancode key)
{
	return key >= 0 && key < SDL_SCANCODE_COUNT && RETRO.keypressed[key];
}

// Consume this frame's accumulated relative mouse motion.
void RETRO_MouseMotion(float *dx, float *dy)
{
	float x = RETRO.mousedx;
	float y = RETRO.mousedy;
	RETRO.mousedx = 0.0f;
	RETRO.mousedy = 0.0f;
	if (RETRO.discardmousemotion) {
		// Wait for the first actual motion, then discard it and resume.
		if (x != 0.0f || y != 0.0f) {
			RETRO.discardmousemotion = false;
		}
		x = 0.0f;
		y = 0.0f;
	}
	if (dx) *dx = x;
	if (dy) *dy = y;
}

// *******************************************************************
// Private functions
// *******************************************************************

void RETRO_Mainloop(void)
{
	while (!RETRO_QuitRequested()) {
		double deltatime = RETRO_DeltaTime();

		// Input handling. Clear this frame's key-press edges and mouse delta,
		// then collect them from the event queue. Events capture every key-down
		// (even a tap that begins and ends within a single frame), so
		// RETRO_KeyPressed never misses a press. The demo reads input state
		// through RETRO_KeyState/RETRO_KeyPressed/RETRO_MouseMotion in DEMO_Input.
		memset(RETRO.keypressed, 0, sizeof(RETRO.keypressed));
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				RETRO.quit = true;
			} else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				RETRO.keypressed[event.key.scancode] = true;
			} else if (event.type == SDL_EVENT_MOUSE_MOTION) {
				RETRO.mousedx += event.motion.xrel;
				RETRO.mousedy += event.motion.yrel;
			} else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
					event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
				RETROGL_UpdateWindowProjection(RETRO.window, &RETRO.width, &RETRO.height,
						RETRO.fov, RETRO.znear, RETRO.zfar);
			}
		}

		// Let the demo poll input once per frame, before rendering
		if (DEMO_Input) DEMO_Input(deltatime);

		// Render the scene
		unsigned long int start = SDL_GetTicks();
		RETROGL_BeginFrame();
		if (DEMO_Render) DEMO_Render(deltatime);
		RETROGL_EndFrame(RETRO.window);
		unsigned long int stop = SDL_GetTicks();

		// Limit FPS
		if (RETRO.fpscap && ((stop - start) < 1000UL / RETRO.fpscap)) {
			SDL_Delay((1000 / RETRO.fpscap) - (stop - start));
		}

		// Show FPS once a second
		if (RETRO.showfps) {
			static unsigned long int fpsticks = SDL_GetTicks();
			static int fpscount = 0;
			if (fpsticks < SDL_GetTicks() - 1000UL) {
				char title[128];
				snprintf(title, 128, "%s - FPS: %d", RETRO.title, fpscount);
				SDL_SetWindowTitle(RETRO.window, title);
				fpsticks = SDL_GetTicks();
				fpscount = 0;
			}
			fpscount++;
		}
	}
}

#endif
