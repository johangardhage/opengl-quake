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
#include <float.h>

#define MOVEMENT_SPEED 5.0

// Everything derived from one BSP texture: its OpenGL objects and classification flags
struct Texture
{
	unsigned int objName = 0;	// OpenGL texture object name
};

// Everything derived from one BSP surface (face): its vertices' lightmap and lighting state
struct Surface
{
	unsigned int lightmapObjName = 0;	// OpenGL lightmap texture object name
};

struct World
{
	RETRO_BSP map;							// The loaded map (BSP, palette and colormap), owned by value

	primdesc_t *surfacePrimitives = NULL;	// Array of surface primitives, contains vertex, texture and lightmap information for every surface
	Texture *textures = NULL;				// Array of per-BSP-texture OpenGL state, one per BSP texture
	Surface *surfaces = NULL;				// Array of per-surface OpenGL state, one per surface
	int *visibleSurfaces = NULL;			// Array of visible surfaces, contains an index to the surfaces
	int numMaxEdgesPerSurface = 0;			// Max edges per surface
	int numTextures = 0;					// Number of OpenGL texture objects
};

World world;
RETRO_Camera camera;

//
// Create one OpenGL texture object per BSP texture and upload mipmapped RGBA data
//
bool UploadTextures(World *world)
{
	world->numTextures = world->map.getNumTextures();
	world->textures = new Texture [world->numTextures];
	for (int i = 0; i < world->numTextures; i++) {
		Texture *texture = &world->textures[i];

		glGenTextures(1, &texture->objName);
		glBindTexture(GL_TEXTURE_2D, texture->objName);

		// Point to the stored mipmaps
		miptex_t *mipTexture = world->map.getMipTexture(i);

		// NULL textures exist, fill their object with a fallback texel.
		if (!mipTexture || !mipTexture->name[0] || mipTexture->offsets[0] == 0) {
			unsigned int filler = 0xFF000000;
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gluBuild2DMipmaps(GL_TEXTURE_2D, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &filler);
			continue;
		}

		int width = mipTexture->width;
		int height = mipTexture->height;
		unsigned int *pixels = new unsigned int [width * height];

		// Point to the raw 8-bit texture data (the full-resolution mip level)
		unsigned char *rawTexture = (unsigned char *)mipTexture + mipTexture->offsets[0];
		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				pixels[x + y * width] = world->map.palette[rawTexture[x + y * width]];
			}
		}

		// Create mipmaps from the created texture
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		delete[] pixels;
	}

	return true;
}

//
// Create an OpenGL lightmap texture for a single surface. Sky and liquid surfaces
// (TEX_SPECIAL), and faces with no stored lighting, get a solid white texel so the
// modulate pass leaves the base texture at full brightness.
//
void BuildLightmap(World *world, int surface, int width, int height)
{
	glBindTexture(GL_TEXTURE_2D, world->surfaces[surface].lightmapObjName);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	dface_t *face = world->map.getSurface(surface);
	texinfo_t *textureInfo = world->map.getTextureInfo(surface);
	unsigned char *samples = world->map.getLightmap(face->lightofs);

	if ((textureInfo->flags & TEX_SPECIAL) || !samples) {
		unsigned char white = 255;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &white);
		return;
	}

	// Combine every light style affecting this surface into a single intensity map.
	// Each active style contributes one width*height block of samples.
	int size = width * height;
	unsigned char *luxels = new unsigned char [size];
	for (int i = 0; i < size; i++) {
		int intensity = 0;
		for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; style++) {
			intensity += samples[style * size + i];
		}
		luxels[i] = (intensity > 255) ? 255 : (unsigned char)intensity;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, luxels);
	delete[] luxels;
}

//
// Build per-surface primitive vertices, lightmap coordinates and lightmap textures
//
bool BuildSurfacePrimitives(World *world)
{
	int numSurfaces = world->map.getNumSurfaces();

	// Allocate memory for the visible surfaces array
	world->visibleSurfaces = new int [world->map.getNumSurfaceLists()];

	// Calculate max number of edges per surface
	world->numMaxEdgesPerSurface = 0;
	for (int i = 0; i < numSurfaces; i++) {
		if (world->numMaxEdgesPerSurface < world->map.getNumEdges(i)) {
			world->numMaxEdgesPerSurface = world->map.getNumEdges(i);
		}
	}

	// Allocate memory for the surface primitive array and one lightmap texture per surface
	world->surfacePrimitives = new primdesc_t [numSurfaces * world->numMaxEdgesPerSurface];
	world->surfaces = new Surface [numSurfaces];
	for (int i = 0; i < numSurfaces; i++) {
		glGenTextures(1, &world->surfaces[i].lightmapObjName);
	}

	// Loop through all the surfaces to fetch the vertices and calculate their texture and lightmap coordinates
	for (int i = 0; i < numSurfaces; i++) {
		int numEdges = world->map.getNumEdges(i);

		// Get a pointer to texinfo for this surface
		texinfo_t *textureInfo = world->map.getTextureInfo(i);
		// Get a pointer to the surface's miptextures (missing texture slots return NULL)
		miptex_t *mipTexture = world->map.getMipTexture(textureInfo->miptex);
		// Fall back to a unit size when the texture slot has no data (missing texture)
		float texWidth = (mipTexture && mipTexture->width) ? (float)mipTexture->width : 1.0f;
		float texHeight = (mipTexture && mipTexture->height) ? (float)mipTexture->height : 1.0f;

		// Point to a surface primitive array
		primdesc_t *primitives = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];

		// Track the surface's texture-space bounds to size its lightmap
		float minS = FLT_MAX, minT = FLT_MAX, maxS = -FLT_MAX, maxT = -FLT_MAX;

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

			// Project the vertex into texture space
			float s = DotProduct(textureInfo->vecs[0], primitives->v) + textureInfo->vecs[0][3];
			float t = DotProduct(textureInfo->vecs[1], primitives->v) + textureInfo->vecs[1][3];

			// Store the normalized texture coords, and stash the raw texture-space
			// coords in the lightmap slot until the bounds are known
			primitives->t[0] = s / texWidth;
			primitives->t[1] = t / texHeight;
			primitives->l[0] = s;
			primitives->l[1] = t;

			if (s < minS) minS = s;
			if (t < minT) minT = t;
			if (s > maxS) maxS = s;
			if (t > maxT) maxT = t;
		}

		// Size the lightmap from the texture-space bounds (one luxel per 16 texels)
		int lightMinS = FloorDiv16(minS);
		int lightMinT = FloorDiv16(minT);
		int lightWidth = CeilDiv16(maxS) - lightMinS + 1;
		int lightHeight = CeilDiv16(maxT) - lightMinT + 1;

		// Convert the stashed texture-space coords into normalized lightmap coords,
		// centred on the luxel (the +8 is half of the 16-texel luxel spacing)
		primitives = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];
		for (int j = 0; j < numEdges; j++, primitives++) {
			primitives->l[0] = (primitives->l[0] - lightMinS * 16 + 8) / (lightWidth * 16.0f);
			primitives->l[1] = (primitives->l[1] - lightMinT * 16 + 8) / (lightHeight * 16.0f);
		}

		// Create the lightmap texture for this surface
		BuildLightmap(world, i, lightWidth, lightHeight);
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

	// Loop through all vertices of the primitive and draw a surface. BSP faces are
	// convex, so a triangle fan from the first vertex fills the whole face.
	glBegin(GL_TRIANGLE_FAN);
	for (int i = 0; i < world->map.getNumEdges(surface); i++, primitives++) {
		glMultiTexCoord2fFn(GL_TEXTURE0, primitives->t[0], primitives->t[1]);
		glMultiTexCoord2fFn(GL_TEXTURE1, primitives->l[0], primitives->l[1]);
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
		int surface = visibleSurfaces[i];
		// Get a pointer to the texture info so we know which base texture to bind
		texinfo_t *textureInfo = world->map.getTextureInfo(surface);
		// Bind the base texture to unit 0 and the surface's lightmap to unit 1
		glActiveTextureFn(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, world->textures[textureInfo->miptex].objName);
		glActiveTextureFn(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, world->surfaces[surface].lightmapObjName);
		// Draw the surface
		DrawSurface(world, surface);
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

	// Lightmapping needs the multitexture entry points the retro lib resolves at startup
	if (!glActiveTextureFn || !glMultiTexCoord2fFn) {
		RETRO_RageQuit("Multitexturing is not supported\n");
	}

	// Build the world from the map
	if (!UploadTextures(&world)) {
		RETRO_RageQuit("Unable to initialize world textures\n");
	}
	if (!BuildSurfacePrimitives(&world)) {
		RETRO_RageQuit("Unable to initialize world surfaces\n");
	}

	// Configure the lightmap texture unit (1) to modulate the base texture on unit 0.
	// GL_COMBINE with an RGB scale of 2 applies "overbright" lighting so lit surfaces are
	// not too dark; the combine defaults already multiply this unit's lightmap texel by the
	// incoming base colour from unit 0.
	glActiveTextureFn(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
	glActiveTextureFn(GL_TEXTURE0);

	// Set the camera's starting position
	camera.SetPosition(540.0f, 260.0f, 100.0f);
	camera.SetOrientation(90.0f, 0.0f);
	camera.SetMovementSpeed(MOVEMENT_SPEED);
	camera.SetFlycam(true);
}

void DEMO_Deinitialize(void)
{
	if (world.textures) {
		for (int i = 0; i < world.numTextures; i++) {
			glDeleteTextures(1, &world.textures[i].objName);
		}
		delete[] world.textures;
		world.textures = NULL;
	}
	if (world.surfaces) {
		for (int i = 0; i < world.map.getNumSurfaces(); i++) {
			glDeleteTextures(1, &world.surfaces[i].lightmapObjName);
		}
		delete[] world.surfaces;
		world.surfaces = NULL;
	}
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
	DrawVisibleSet(&world, leaf);
}
