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

void DEMO_Startup(void)
{
	RETRO.title = "Quake!";
	RETRO.fov = 75.0;
	RETRO.znear = 1.0;
	RETRO.zfar = 5000.0;
}

void DEMO_Initialize(void)
{
}

void DEMO_Deinitialize(void)
{
}

void DEMO_Input(double deltatime)
{
}

void DEMO_Render(double deltatime)
{
}
