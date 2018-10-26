//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#ifndef _WORLD_H_
#define _WORLD_H_

#include "Map.h"
#include "Camera.h"

class World
{
private:
	Map *map;

	primdesc_t *surfacePrimitives;	// Array of surface primitives, contains vertex and texture information for every surface
	unsigned int *textureObjNames;	// Array of available texture object names, the name is a number
	int *visibleSurfaces;			// Array of visible surfaces, contains an index to the surfaces
	int numMaxEdgesPerSurface;		// Max edges per surface

	bspleaf_t *FindLeaf(Camera *camera);
	float CalculateDistance(vec3_t a, vec3_t b);
	void DrawSurface(int surface);
	void DrawSurfaceList(int *visibleSurfaces, int numVisibleSurfaces);
	void DrawLeafVisibleSet(bspleaf_t *pLeaf);
	bool InitializeSurfaces(void);
	bool InitializeTextures(void);

public:
	World() {
        surfacePrimitives = NULL;
        textureObjNames = NULL;
		visibleSurfaces = NULL;
    }
	~World() {
		if (surfacePrimitives) delete[] surfacePrimitives;
		if (textureObjNames) delete[] textureObjNames;
		if (visibleSurfaces) delete[] visibleSurfaces;
	}

	bool Initialize(Map *map, int width, int height);
	void DrawScene(Camera *camera);
};

#endif
