//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#define RETRO_WIDTH 320
#define RETRO_HEIGHT 200
#include "lib/retromain.h"
#include "lib/retrobsp.h"
#include "lib/retromath.h"
#include "lib/retrocamera.h"

#define MOVEMENT_SPEED 5.0

struct World
{
	RETRO_BSP map;							// The loaded map (BSP, palette and colormap), owned by value
};

World world;
RETRO_Camera camera;

void DEMO_Startup(void)
{
	RETRO.title = "Quake!";
	RETRO.usagekeys = "WASD/arrows move, mouse look, PageUp/PageDown pitch, F toggle flycam, Space/Ctrl up/down";
	RETRO.fov = 75.0;
	RETRO.znear = 1.0;
	RETRO.zfar = 5000.0;
}

void DEMO_Initialize(void)
{
	// Load the map (BSP, palette and colormap)
	world.map = RETRO_LoadBSP("assets/start.bsp", "assets/palette.lmp", "assets/colormap.lmp");
	if (!world.map.bsp) {
		RETRO_RageQuit("Unable to load BSP\n");
	}

	// Set the camera's starting position
	camera.SetPosition(540.0f, 260.0f, 100.0f);
	camera.SetOrientation(90.0f, 0.0f);
	camera.SetMovementSpeed(MOVEMENT_SPEED);
	camera.SetFlycam(true);
}

void DEMO_Deinitialize(void)
{
	RETRO_FreeBSP(&world.map);
}

void DEMO_Input(double deltatime)
{
	// Keyboard handling
	float scale = (float)(deltatime * RETRO_INPUT_FRAMERATE);

	if (RETRO_KeyState(SDL_SCANCODE_W) || RETRO_KeyState(SDL_SCANCODE_UP)) {
		camera.MoveForward(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_S) || RETRO_KeyState(SDL_SCANCODE_DOWN)) {
		camera.MoveBackward(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_D)) {
		camera.StrafeRight(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_A)) {
		camera.StrafeLeft(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_RIGHT)) {
		camera.TurnRight(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_LEFT)) {
		camera.TurnLeft(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_PAGEUP)) {
		camera.PitchUp(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_PAGEDOWN)) {
		camera.PitchDown(scale);
	}
	if (RETRO_KeyPressed(SDL_SCANCODE_F)) {
		camera.SetFlycam(!camera.flycam);
	}
	if (camera.flycam && RETRO_KeyState(SDL_SCANCODE_SPACE)) {
		camera.MoveUp(scale);
	}
	if (camera.flycam && (RETRO_KeyState(SDL_SCANCODE_LCTRL) || RETRO_KeyState(SDL_SCANCODE_RCTRL))) {
		camera.MoveDown(scale);
	}

	// Mouse handling
	if (!RETRO.showcursor) {
		float xrel = 0.0f;
		float yrel = 0.0f;
		RETRO_MouseMotion(&xrel, &yrel);
		camera.MouseLook(xrel, yrel);
	}

	camera.Update();
}

void DEMO_Render(double deltatime)
{
	// Setup a viewing matrix and transformation (the framebuffer clear and the
	// modelview reset are handled by the RETRO main loop before this is called)
	gluLookAt(camera.origin[0], camera.origin[1], camera.origin[2],
			camera.origin[0] + camera.forward[0],
			camera.origin[1] + camera.forward[1],
			camera.origin[2] + camera.forward[2],
			camera.up[0], camera.up[1], camera.up[2]);

	// Render the scene
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_POINTS);
	for (int i = 0; i < world.map.getLump(LUMP_VERTEXES)->filelen / sizeof(dvertex_t); i++) {
		glVertex3fv((float *)world.map.getVertex(i));
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
}
