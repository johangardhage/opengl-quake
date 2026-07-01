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

	primdesc_t *surfacePrimitives = NULL;	// Array of surface primitives, contains vertex information for every surface
	int *visibleSurfaces = NULL;			// Array of visible surfaces, contains an index to the surfaces
	int numMaxEdgesPerSurface = 0;			// Max edges per surface
};

World world;
RETRO_Camera camera;

//
// Build per-surface primitive vertices
//
bool BuildSurfacePrimitives(World *world)
{
	// Allocate memory for the visible surfaces array
	world->visibleSurfaces = new int [world->map.getNumSurfaceLists()];

	// Calculate max number of edges per surface
	world->numMaxEdgesPerSurface = 0;
	for (int i = 0; i < world->map.getNumSurfaces(); i++) {
		if (world->numMaxEdgesPerSurface < world->map.getNumEdges(i)) {
			world->numMaxEdgesPerSurface = world->map.getNumEdges(i);
		}
	}

	// Allocate memory for the surface primitive array
	world->surfacePrimitives = new primdesc_t [world->map.getNumSurfaces() * world->numMaxEdgesPerSurface];

	// Loop through all the surfaces to fetch the vertices
	for (int i = 0; i < world->map.getNumSurfaces(); i++) {
		int numEdges = world->map.getNumEdges(i);

		// Point to a surface primitive array
		primdesc_t *primitives = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];

		for (int j = 0; j < numEdges; j++, primitives++) {
			// Get an edge id from the surface. Fetch the correct edge by using the id in the Edge List.
			// The winding is backwards!
			int edgeId = world->map.getEdgeList(world->map.getSurface(i)->firstedge + (numEdges - 1 - j));
			// Positive surfedge -> edge used forwards (start vertex); otherwise reversed (end vertex)
			int vertexId = ((edgeId >= 0) ? world->map.getEdge(edgeId)->v[0] : world->map.getEdge(-edgeId)->v[1]);

			// Store the vertex in the primitive array
			vec3_t *vertex = world->map.getVertex(vertexId);
			primitives->v[0] = ((float *)vertex)[0];
			primitives->v[1] = ((float *)vertex)[1];
			primitives->v[2] = ((float *)vertex)[2];
		}
	}

	return true;
}

//
// Draw the surface
//
void DrawSurface(World *world, int surface)
{
	// Get the surface primitive
	primdesc_t *primitives = &world->surfacePrimitives[world->numMaxEdgesPerSurface * surface];

	// Give each surface a deterministic flat colour derived from its index
	unsigned int hash = (unsigned int)surface * 1103515245u + 12345u;
	glColor3ub(hash & 0xff, (hash >> 8) & 0xff, (hash >> 16) & 0xff);

	// Loop through all vertices of the primitive and draw a surface. BSP faces are
	// convex, so a triangle fan from the first vertex fills the whole face.
	glBegin(GL_TRIANGLE_FAN);
	for (int i = 0; i < world->map.getNumEdges(surface); i++, primitives++) {
		glVertex3fv(primitives->v);
	}
	glEnd();
}

//
// Draw the visible surfaces
//
void DrawSurfaces(World *world, int *visibleSurfaces, int numVisibleSurfaces)
{
	// Loop through all the visible surfaces and draw them
	for (int i = 0; i < numVisibleSurfaces; i++) {
		DrawSurface(world, visibleSurfaces[i]);
	}
}

//
// Calculate which other leaves are visible from the specified leaf, fetch the associated surfaces and draw them
//
void DrawVisibleSet(World *world, dleaf_t *pLeaf)
{
	int numVisibleSurfaces = 0;
	// Leaves are numbered 1..numLeaves; bit (i-1) of the PVS maps to leaf i.
	int numLeaves = world->map.getNumLeaves();

	if (pLeaf->visofs < 0) {
		// No visibility information for this leaf: treat every leaf as potentially visible.
		for (int i = 1; i <= numLeaves; i++) {
			dleaf_t *visibleLeaf = world->map.getLeaf(i);
			int firstSurface = visibleLeaf->firstmarksurface;
			int lastSurface = firstSurface + visibleLeaf->nummarksurfaces;
			for (int k = firstSurface; k < lastSurface; k++) {
				world->visibleSurfaces[numVisibleSurfaces++] = world->map.getSurfaceList(k);
			}
		}
	} else {
		// Decompress the run-length encoded PVS. A zero byte means "skip the next
		// (8 * following byte) leaves"; any other byte holds 8 visibility bits,
		// least-significant bit first.
		unsigned char *visibilityList = world->map.getVisibilityList(pLeaf->visofs);
		for (int i = 1; i <= numLeaves; ) {
			if (*visibilityList == 0) {
				i += 8 * visibilityList[1];
				visibilityList += 2;
			} else {
				for (int bit = 1; bit < 256 && i <= numLeaves; bit <<= 1, i++) {
					if (*visibilityList & bit) {
						// Fetch the leaf that is seen and copy its surfaces
						dleaf_t *visibleLeaf = world->map.getLeaf(i);
						int firstSurface = visibleLeaf->firstmarksurface;
						int lastSurface = firstSurface + visibleLeaf->nummarksurfaces;
						for (int k = firstSurface; k < lastSurface; k++) {
							world->visibleSurfaces[numVisibleSurfaces++] = world->map.getSurfaceList(k);
						}
					}
				}
				visibilityList++;
			}
		}
	}

	// Draw the copied surfaces
	DrawSurfaces(world, world->visibleSurfaces, numVisibleSurfaces);
}

//
// Traverse the BSP tree to find the leaf containing the camera
//
dleaf_t *FindCameraLeaf(World *world, RETRO_Camera *camera)
{
	dleaf_t *leaf = NULL;

	// Fetch the start node
	dnode_t *node = world->map.getStartNode();

	while (!leaf) {
		short nextNodeId;

		// Get a pointer to the plane which intersects the node
		dplane_t *plane = world->map.getPlane(node->planenum);

		// Calculate distance to the intersecting plane
		float distance = DotProduct(plane->normal, camera->origin);

		// If the camera is in front of the plane, traverse the right (front) node, otherwise traverse the left (back) node
		if (distance > plane->dist) {
			nextNodeId = node->children[0];
		} else {
			nextNodeId = node->children[1];
		}

		// If next node >= 0, traverse the node, otherwise use the inverse of the node as the index to the leaf (and we are done!)
		if (nextNodeId >= 0) {
			node = world->map.getNode(nextNodeId);
		} else {
			leaf = world->map.getLeaf(~nextNodeId);
		}
	}

	return leaf;
}

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

	// Build the world from the map
	if (!BuildSurfacePrimitives(&world)) {
		RETRO_RageQuit("Unable to initialize world surfaces\n");
	}

	// Set the camera's starting position
	camera.SetPosition(540.0f, 260.0f, 100.0f);
	camera.SetOrientation(90.0f, 0.0f);
	camera.SetMovementSpeed(MOVEMENT_SPEED);
	camera.SetFlycam(true);
}

void DEMO_Deinitialize(void)
{
	if (world.surfacePrimitives) delete[] world.surfacePrimitives;
	if (world.visibleSurfaces) delete[] world.visibleSurfaces;
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

	// Find the leaf the camera is in
	dleaf_t *leaf = FindCameraLeaf(&world, &camera);

	// Render the scene
	glDisable(GL_TEXTURE_2D);
	DrawVisibleSet(&world, leaf);
	glEnable(GL_TEXTURE_2D);
}
