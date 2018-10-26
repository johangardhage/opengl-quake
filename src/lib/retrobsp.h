//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//

#ifndef _RETROBSP_H_
#define _RETROBSP_H_

#include <fcntl.h> // open
#include <stdio.h> // printf
#include <stdlib.h> // malloc, free
#include <sys/stat.h> // stat
#include <unistd.h> // read, close

#define BSP_VERSION			29
#define HEADER_LUMPS		15
#define MAX_MAP_HULLS		4
#define MIPLEVELS			4
#define MAXLIGHTMAPS		4
#define NUM_AMBIENTS		4

// Quake BSP version 29 lump order, from id Software's bspfile.h.
enum
{
	LUMP_ENTITIES = 0,
	LUMP_PLANES,
	LUMP_TEXTURES,
	LUMP_VERTEXES,
	LUMP_VISIBILITY,
	LUMP_NODES,
	LUMP_TEXINFO,
	LUMP_FACES,
	LUMP_LIGHTING,
	LUMP_CLIPNODES,
	LUMP_LEAFS,
	LUMP_MARKSURFACES,
	LUMP_EDGES,
	LUMP_SURFEDGES,
	LUMP_MODELS
};

// Values for dplane_t.type: the axis a plane is perpendicular to, or ANY* when not axis-aligned.
enum
{
	PLANE_X = 0,
	PLANE_Y,
	PLANE_Z,
	PLANE_ANYX,
	PLANE_ANYY,
	PLANE_ANYZ
};

// Values for dleaf_t.contents and clipnode children: what fills a region of space.
enum
{
	CONTENTS_EMPTY = -1,
	CONTENTS_SOLID = -2,
	CONTENTS_WATER = -3,
	CONTENTS_SLIME = -4,
	CONTENTS_LAVA = -5,
	CONTENTS_SKY = -6,
	CONTENTS_ORIGIN = -7,
	CONTENTS_CLIP = -8,
	CONTENTS_CURRENT_0 = -9,
	CONTENTS_CURRENT_90 = -10,
	CONTENTS_CURRENT_180 = -11,
	CONTENTS_CURRENT_270 = -12,
	CONTENTS_CURRENT_UP = -13,
	CONTENTS_CURRENT_DOWN = -14
};

#define TEX_SPECIAL 1	// texinfo_t.flags bit: sky or liquid surface, drawn without a lightmap

//
// On-disk BSP structures. These are laid out to match the file image
// byte-for-byte (little-endian); the static_assert checks at the end of this
// file lock each struct to the size required by the format.
//

// A lump is one section of the BSP file.
struct lump_t
{
	int fileofs;	// Offset to the lump, in bytes, from the start of the file
	int filelen;	// Length of the lump, in bytes
};

// BSP file header: version followed by the lump directory.
struct dheader_t
{
	int version;				// BSP version, must be BSP_VERSION (29) for Quake
	lump_t lumps[HEADER_LUMPS];	// Directory of every lump, indexed by LUMP_*
};

typedef float vec2_t[2];	// 2D coordinate (texture or lightmap S,T)
typedef float vec3_t[3];	// 3D coordinate (X,Y,Z)
typedef short svec3_t[3];	// 3D coordinate stored as shorts (bounding boxes)

// A model: a self-contained group of faces. Model 0 is the world.
struct dmodel_t
{
	vec3_t mins;					// Bounding box minimum
	vec3_t maxs;					// Bounding box maximum
	vec3_t origin;					// Model origin (zero for the world)
	int headnode[MAX_MAP_HULLS];	// Root node of each hull; headnode[0] is the render BSP
	int visleafs;					// Number of visible leaves, excluding the solid leaf 0
	int firstface;					// First face in the face lump
	int numfaces;					// Number of faces
};

// Header of the texture lump: a count followed by an offset to each texture.
struct dmiptexlump_t
{
	int nummiptex;	// Number of textures
	int dataofs[1];	// [nummiptex] offsets to each miptex_t, or -1 when the texture is missing
};

// A mip texture: name, dimensions and offsets to its four mip levels.
struct miptex_t
{
	char name[16];						// Name of the texture
	unsigned int width;					// Width in texels, a multiple of 8
	unsigned int height;				// Height in texels, a multiple of 8
	unsigned int offsets[MIPLEVELS];	// Offsets to the four mip levels, from the start of this miptex_t
};

// A single map vertex.
struct dvertex_t
{
	vec3_t point;	// X,Y,Z position
};

// A splitting plane.
struct dplane_t
{
	vec3_t normal;	// Unit normal (Nx^2 + Ny^2 + Nz^2 = 1)
	float dist;		// Distance from the origin along the normal
	int type;		// Axis alignment, PLANE_X..PLANE_ANYZ
};

// Texture mapping info shared by faces.
struct texinfo_t
{
	float vecs[2][4];	// S and T projection vectors; vecs[i] = (x, y, z, offset)
	int miptex;			// Index into the texture lump
	int flags;			// Surface flags (TEX_SPECIAL)
};

// A face: one drawable convex polygon.
struct dface_t
{
	short planenum;						// Plane the face lies on
	short side;							// 0 if the face faces along the plane normal, 1 if opposed
	int firstedge;						// First entry in the surfedge lump
	short numedges;						// Number of edges (and vertices) in the face
	short texinfo;						// Index into the texinfo lump
	unsigned char styles[MAXLIGHTMAPS];	// Light styles affecting the face, 0xFF ends the list
	int lightofs;						// Byte offset into the lighting lump, or -1 for no lightmap
};

// An internal BSP node.
struct dnode_t
{
	int planenum;				// Splitting plane, index into the plane lump
	short children[2];			// Front/back child: >= 0 is a node index, < 0 is leaf ~child
	short mins[3];				// Bounding box minimum, for culling
	short maxs[3];				// Bounding box maximum, for culling
	unsigned short firstface;	// First face in the face lump
	unsigned short numfaces;	// Number of faces
};

// A collision-hull node (not used for rendering).
struct dclipnode_t
{
	int planenum;		// Splitting plane
	short children[2];	// Front/back child: >= 0 is a node index, < 0 is a CONTENTS_* value
};

// An edge: a pair of vertex indices.
struct dedge_t
{
	unsigned short v[2];	// Start and end vertex indices
};

// A BSP leaf: a convex region of space.
struct dleaf_t
{
	int contents;								// CONTENTS_* describing what fills the leaf
	int visofs;									// Offset into the visibility lump, or -1 for no vis info
	short mins[3];								// Bounding box minimum, for culling
	short maxs[3];								// Bounding box maximum, for culling
	unsigned short firstmarksurface;			// First entry in the marksurface lump
	unsigned short nummarksurfaces;				// Number of marksurfaces (faces) in the leaf
	unsigned char ambient_level[NUM_AMBIENTS];	// Ambient sound volumes (0 = silent, 0xFF = max)
};

// A renderer-side vertex: world position plus texture and lightmap coordinates.
struct primdesc_t
{
	vec2_t t;	// Texture coordinate
	vec2_t l;	// Lightmap coordinate
	vec3_t v;	// Vertex coordinate
};

struct RETRO_BSP
{
	char *bsp;
	dheader_t *header;
	unsigned char *colormap;
	unsigned int palette[256];

	// Get a lump directory entry by LUMP_* index
	lump_t *getLump(int lump) { return &header->lumps[lump]; }

	// Get the number of edges (and vertices) of a surface
	int getNumEdges(int surfaceId) { return getSurface(surfaceId)->numedges; }

	// Get the number of surfaces (faces)
	int getNumSurfaces() { return getLump(LUMP_FACES)->filelen / sizeof(dface_t); }

	// Get the number of textures
	int getNumTextures() { return ((dmiptexlump_t *)getMipHeader())->nummiptex; }

	// Get the number of marksurface entries
	int getNumSurfaceLists() { return getLump(LUMP_MARKSURFACES)->filelen / sizeof(unsigned short); }

	// Get the number of visible leaves
	int getNumLeaves() { return getModel(0)->visleafs; }

	// Get one vertex
	vec3_t *getVertex(int id) { return &((vec3_t *)&bsp[getLump(LUMP_VERTEXES)->fileofs])[id]; }

	// Get one edge (holds a start and end vertex index)
	dedge_t *getEdge(int id) { return &((dedge_t *)&bsp[getLump(LUMP_EDGES)->fileofs])[id]; }

	// Get one surfedge entry: a signed edge index, negative when the edge is reversed
	int getEdgeList(int id) { return ((int *)&bsp[getLump(LUMP_SURFEDGES)->fileofs])[id]; }

	// Get one plane
	dplane_t *getPlane(int id) { return &((dplane_t *)&bsp[getLump(LUMP_PLANES)->fileofs])[id]; }

	// Get one surface (face)
	dface_t *getSurface(int id) { return &((dface_t *)&bsp[getLump(LUMP_FACES)->fileofs])[id]; }

	// Get one marksurface entry: a face index referenced by a leaf
	unsigned short getSurfaceList(int id) { return ((unsigned short *)&bsp[getLump(LUMP_MARKSURFACES)->fileofs])[id]; }

	// Get one model (model 0 is the world and the main render hull)
	dmodel_t *getModel(int id) { return &((dmodel_t *)&bsp[getLump(LUMP_MODELS)->fileofs])[id]; }

	// Get one BSP node
	dnode_t *getNode(int id) { return &((dnode_t *)&bsp[getLump(LUMP_NODES)->fileofs])[id]; }

	// Get the root node of the render BSP (model 0)
	dnode_t *getStartNode() { return getNode(getModel(0)->headnode[0]); }

	// Get one BSP leaf
	dleaf_t *getLeaf(int id) { return &((dleaf_t *)&bsp[getLump(LUMP_LEAFS)->fileofs])[id]; }

	// Get the visibility list (run-length encoded PVS) at a dleaf_t.visofs offset
	unsigned char *getVisibilityList(int offset) { return (unsigned char *)&bsp[getLump(LUMP_VISIBILITY)->fileofs] + offset; }

	// Get the texture lump header (dmiptexlump_t)
	unsigned char *getMipHeader() { return (unsigned char *)&bsp[getLump(LUMP_TEXTURES)->fileofs]; }

	// Get one mip texture (NULL when the slot has no data, i.e. dataofs == -1 for a missing texture)
	miptex_t *getMipTexture(int id) {
		int offset = ((dmiptexlump_t *)getMipHeader())->dataofs[id];
		return offset >= 0 ? (miptex_t *)(getMipHeader() + offset) : NULL;
	}

	// Get the lightmap samples at an offset, or NULL when there is no lightmap
	unsigned char *getLightmap(int offset) { return offset >= 0 ? (unsigned char *)&bsp[getLump(LUMP_LIGHTING)->fileofs] + offset : NULL; }

	// Get the colormap (shading table loaded separately from the BSP)
	unsigned char *getColormap() { return colormap; }

	// Get the texinfo for a surface
	texinfo_t *getTextureInfo(int id) { return &((texinfo_t *)&bsp[getLump(LUMP_TEXINFO)->fileofs])[getSurface(id)->texinfo]; }
};

static_assert(sizeof(lump_t) == 8, "lump_t must match Quake BSP");
static_assert(sizeof(dheader_t) == 124, "dheader_t must match Quake BSP");
static_assert(sizeof(dmodel_t) == 64, "dmodel_t must match Quake BSP");
static_assert(sizeof(miptex_t) == 40, "miptex_t must match Quake BSP");
static_assert(sizeof(dvertex_t) == 12, "dvertex_t must match Quake BSP");
static_assert(sizeof(dplane_t) == 20, "dplane_t must match Quake BSP");
static_assert(sizeof(dnode_t) == 24, "dnode_t must match Quake BSP");
static_assert(sizeof(dclipnode_t) == 8, "dclipnode_t must match Quake BSP");
static_assert(sizeof(texinfo_t) == 40, "texinfo_t must match Quake BSP");
static_assert(sizeof(dface_t) == 20, "dface_t must match Quake BSP");
static_assert(sizeof(dedge_t) == 4, "dedge_t must match Quake BSP");
static_assert(sizeof(dleaf_t) == 28, "dleaf_t must match Quake BSP");

//
// Read a whole file into a freshly allocated buffer. Returns the byte count, or 0
// on any failure (and leaves *bufferptr untouched).
//
inline int RETRO_LoadFile(const char *filename, void **bufferptr)
{
	struct stat st;
	void *buffer;
	int length;

	int f = open(filename, O_RDONLY);
	if (f == -1) {
		return 0;
	}

	if (stat(filename, &st) != 0) {
		close(f);
		return 0;
	}
	length = st.st_size;
	buffer = malloc(length);
	if (!buffer) {
		close(f);
		return 0;
	}
	if (read(f, buffer, length) != length) {
		free(buffer);
		close(f);
		return 0;
	}
	close(f);

	*bufferptr = buffer;
	return length;
}

//
// Load the 256-entry RGB palette and pack it into 0x00BBGGRR words
//
inline bool RETRO_LoadBSPPalette(RETRO_BSP *bsp, const char *filename)
{
	unsigned char *tempPal = NULL;

	int length = RETRO_LoadFile(filename, (void **)&tempPal);
	if (length < 256 * 3) {
		if (tempPal) free(tempPal);
		return false;
	}

	for (int i = 0; i < 256; i++) {
		unsigned int r = tempPal[i * 3 + 0];
		unsigned int g = tempPal[i * 3 + 1];
		unsigned int b = tempPal[i * 3 + 2];
		bsp->palette[i] = (r) | (g << 8) | (b << 16);
	}

	free(tempPal);
	return true;
}

//
// Load the colormap: 64 shaded rows of 256 palette indices, used for lightmapping
//
inline bool RETRO_LoadBSPColormap(RETRO_BSP *bsp, const char *filename)
{
	int length = RETRO_LoadFile(filename, (void **)&bsp->colormap);
	if (!length) {
		return false;
	}
	if (length < 256 * 64) {
		printf("[ERROR] RETRO_LoadBSPColormap() Colormap file is too small!\n");
		return false;
	}
	return true;
}

//
// Load the BSP file into memory and verify its version
//
inline bool RETRO_LoadBSPMap(RETRO_BSP *bsp, const char *filename)
{
	if (!RETRO_LoadFile(filename, (void **)&bsp->bsp)) {
		return false;
	}

	bsp->header = (dheader_t *)bsp->bsp;
	if (bsp->header->version != BSP_VERSION) {
		printf("[ERROR] RETRO_LoadBSPMap() BSP file version mismatch!\n");
		return false;
	}

	return true;
}

//
// Release BSP and colormap allocations
//
inline void RETRO_FreeBSP(RETRO_BSP *bsp)
{
	if (bsp->bsp) {
		free(bsp->bsp);
		bsp->bsp = NULL;
	}
	if (bsp->colormap) {
		free(bsp->colormap);
		bsp->colormap = NULL;
	}
}

//
// Load the BSP, palette and colormap that the renderer needs
//
inline RETRO_BSP RETRO_LoadBSP(const char *bspFilename, const char *paletteFilename, const char *colormapFilename)
{
	RETRO_BSP bsp;
	bsp.bsp = NULL;
	bsp.header = NULL;
	bsp.colormap = NULL;

	if (!RETRO_LoadBSPMap(&bsp, bspFilename)) {
		printf("[ERROR] RETRO_LoadBSP() Error loading bsp file\n");
		RETRO_FreeBSP(&bsp);
		return bsp;
	}

	if (!RETRO_LoadBSPPalette(&bsp, paletteFilename)) {
		printf("[ERROR] RETRO_LoadBSP() Error loading palette\n");
		RETRO_FreeBSP(&bsp);
		return bsp;
	}

	if (!RETRO_LoadBSPColormap(&bsp, colormapFilename)) {
		printf("[ERROR] RETRO_LoadBSP() Error loading colormap\n");
		RETRO_FreeBSP(&bsp);
		return bsp;
	}

	return bsp;
}

#endif
