//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#include <GL/glu.h>
#include <string.h> // strncmp
#include "World.h"
#include "Camera.h"

//
// Initialize World
//
bool World::Initialize(Map *map, int width, int height)
{
	this->map = map;

	// Enable OpenGL features
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glDrawBuffer(GL_BACK);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Setup the viewport and frustum
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	double ratio = (double)height / (double)width;
	glFrustum(-1.0, 1.0, -ratio, ratio, 1.0, 5000);

	// Build textures
	if (!InitializeTextures()) {
		return false;
	}

	// Calculate surfaces
	if (!InitializeSurfaces()) {
		return false;
	}

	return true;
}

//
// Draw the scene
//
void World::DrawScene(Camera *camera)
{
	// Clear the drawbuffer
	glClear(GL_DEPTH_BUFFER_BIT);

	// Setup a viewing matrix and transformation
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(camera->head[0], camera->head[1], camera->head[2],
			camera->head[0] + camera->view[0],
			camera->head[1] + camera->view[1],
			camera->head[2] + camera->view[2],
			0, 0, 1);

	// Find the leaf the camera is in
	bspleaf_t *leaf = FindLeaf(camera);

	// Render the scene
	DrawLeafVisibleSet(leaf);
}

//
// Draw the surface
//
void World::DrawSurface(int surface)
{
	// Get the surface primitive
	primdesc_t *primitives = &surfacePrimitives[numMaxEdgesPerSurface * surface];

	// Loop through all vertices of the primitive and draw a surface
	glBegin(GL_POLYGON);
	for (int i = 0; i < map->getNumEdges(surface); i++, primitives++) {
		glTexCoord2fv(primitives->t);
		glVertex3fv(primitives->v);
	}
	glEnd();
}  

//
// Draw the visible surfaces
//
void World::DrawSurfaceList(int *visibleSurfaces, int numVisibleSurfaces)
{
	// Loop through all the visible surfaces and activate the texture objects
	for (int i = 0; i < numVisibleSurfaces; i++) {
		// Get a pointer to the texture info so we know which texture we should bind and draw
		texinfo_t *textureInfo = map->getTextureInfo(visibleSurfaces[i]);
		// Bind the previously created texture object
		glBindTexture(GL_TEXTURE_2D, textureObjNames[textureInfo->texid]);
		// Draw the surface
		DrawSurface(visibleSurfaces[i]);
	}
}

//
// Calculate which other leaves are visible from the specified leaf, fetch the associated surfaces and draw them
//
void World::DrawLeafVisibleSet(bspleaf_t *pLeaf)
{
	int numVisibleSurfaces = 0;

	// Get a pointer to the visibility list that is associated with the BSP leaf
	unsigned char *visibilityList = map->getVisibilityList(pLeaf->vislist);

	// Loop trough all leaves to see if they are visible
	for (int i = 1; i < map->getNumLeaves(); visibilityList++) {
		// Check the visibility list if the current leaf is seen
		if (*visibilityList) {
			// Loop through the visible leaves
			for (int j = 0; j < 8; j++, i++) {
				// Fetch the leaf that is seen from the array of leaves
				bspleaf_t *visibleLeaf = map->getLeaf(i);
				// Copy all the visible surfaces from the List of surfaces
				int firstSurface = visibleLeaf->firstsurf;
				int lastSurface = firstSurface + visibleLeaf->numsurf;
				for (int k = firstSurface; k < lastSurface; k++) {
					visibleSurfaces[numVisibleSurfaces++] = map->getSurfaceList(k);
				}
			}
		} else {
			// No, skip some leaves
			i += (*visibilityList++) << 3;
		}
	}

	// Draw the copied surfaces
	DrawSurfaceList(visibleSurfaces, numVisibleSurfaces);
}

//
// Traverse the BSP tree to find the leaf containing visible surfaces from a specific position
//
bspleaf_t *World::FindLeaf(Camera *camera)
{
	bspleaf_t *leaf = NULL;

	// Fetch the start node
	bspnode_t *node = map->getStartNode();

	while (!leaf) {
		short nextNodeId;

		// Get a pointer to the plane which intersects the node
		plane_t *plane = map->getPlane(node->planenum);

		// Calculate distance to the intersecting plane
		float distance = CalculateDistance(plane->normal, camera->head);

		// If the camera is in front of the plane, traverse the right (front) node, otherwise traverse the left (back) node
		if (distance > plane->dist) {
			nextNodeId = node->front;
		} else {
			nextNodeId = node->back;
		}

		// If next node >= 0, traverse the node, otherwise use the inverse of the node as the index to the leaf (and we are done!)
		if (nextNodeId >= 0) {
			node = map->getNode(nextNodeId);
		} else {
			leaf = map->getLeaf(~nextNodeId);
		}
	}

	return leaf;
}

//
// Build mipmaps from the textures, create texture objects and assign mipmaps to the created objects
//
bool World::InitializeTextures(void)
{
	int numAnimTextues = 0;
	int numSkyTextures = 0;

	// Loop through all textures to count animations and sky textures
	for (int i = 0; i < map->getNumTextures(); i++) {
		// Point to the stored mipmaps
		miptex_t *mipTexture = map->getMipTexture(i);
		if (strncmp(mipTexture->name, "*", 1) == 0) {
			numAnimTextues++;
		} else if (strncmp(mipTexture->name, "sky", 3) == 0) {
			numSkyTextures++;
		}
	}
	//printf("%d animated textures, %d sky textures\n", numAnimTextues, numSkyTextures);

	// Calculate the total number of textures
	int numTotalTextures = map->getNumTextures() + numAnimTextues * (ANIM_TEX_FRAMES - 1) + numSkyTextures * 2;

	// Allocate memory for texture names and an array to store the textures we are using
	textureObjNames = new unsigned int [numTotalTextures];

	// Get unused texture names
	glGenTextures(numTotalTextures, textureObjNames);

	// Loop through all texture objects to associate them with a texture and calculate mipmaps from it
	for (int i = 0; i < map->getNumTextures(); i++) {
		// Point to the stored mipmaps
		miptex_t *mipTexture = map->getMipTexture(i);

		// NULL textures exist, don't use them!
		if (!mipTexture->name[0]) {
			continue;
		}

		int width = mipTexture->width;
		int height = mipTexture->height;

		// Allocate memory for the texture which will be created
		unsigned int *texture = new unsigned int [width * height];

		// Point to the raw 8-bit texture data
		unsigned char *rawTexture = map->getRawTexture(i);

		// Create a texture to assign with the texture object
		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				texture[x + y * width] = map->palette[rawTexture[x + y * width]];
			}
		}

		// Create a new texture object
		glBindTexture(GL_TEXTURE_2D, textureObjNames[i]);
		// Create mipmaps from the created texture
		gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, texture);

		delete texture;
	}

	return true;
}

//
// Calculate primitive surfaces
//
bool World::InitializeSurfaces(void)
{
	// Allocate memory for the visible surfaces array
	visibleSurfaces = new int [map->getNumSurfaceLists()];

	// Calculate max number of edges per surface
	numMaxEdgesPerSurface = 0;
	for (int i = 0; i < map->getNumSurfaces(); i++) {
		if (numMaxEdgesPerSurface < map->getNumEdges(i)) {
			numMaxEdgesPerSurface = map->getNumEdges(i);
		}
	}

	// Allocate memory for the surface primitive array
	surfacePrimitives = new primdesc_t [map->getNumSurfaces() * numMaxEdgesPerSurface];

	// Loop through all the surfaces to fetch the vertices and calculate it's texture coords
	for (int i = 0; i < map->getNumSurfaces(); i++) {
		int numEdges = map->getNumEdges(i);

		// Get a pointer to texinfo for this surface
		texinfo_t *textureInfo = map->getTextureInfo(i);
		// Get a pointer to the surface's miptextures
		miptex_t *mipTexture = map->getMipTexture(textureInfo->texid);

		// Point to a surface primitive array
		primdesc_t *primitives = &surfacePrimitives[i * numMaxEdgesPerSurface];

		for (int j = 0; j < numEdges; j++, primitives++) {
			// Get an edge id from the surface. Fetch the correct edge by using the id in the Edge List.
			// The winding is backwards!
			int edgeId = map->getEdgeList(map->getSurface(i)->firstedge + (numEdges - 1 - j));
			// Fetch one of the egde's vertex
			int vertexId = ((edgeId >= 0) ? map->getEdge(edgeId)->startVertex : map->getEdge(-edgeId)->endVertex);

			// Store the vertex in the primitive array
			vec3_t *vertex = map->getVertex(vertexId);
			primitives->v[0] = ((float *)vertex)[0];
			primitives->v[1] = ((float *)vertex)[1];
			primitives->v[2] = ((float *)vertex)[2];

			// Calculate the vertex's texture coords and store it in the primitive array
			primitives->t[0] = (CalculateDistance(textureInfo->snrm, primitives->v) + textureInfo->soff) / mipTexture->width;
			primitives->t[1] = (CalculateDistance(textureInfo->tnrm, primitives->v) + textureInfo->toff) / mipTexture->height;
		}
	}

	return true;
}

//
// Return the dotproduct of two vectors
//
float World::CalculateDistance(vec3_t a, vec3_t b)
{
	return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]); 
}
