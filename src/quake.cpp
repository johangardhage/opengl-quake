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

// Liquid surfaces ripple their texture coordinates; sky surfaces use Quake's
// two-layer sky projection.
#define WARP_SPACE_FREQ 6.28f	// ripple cycles per texture repeat
#define WARP_TIME_FREQ 2.0f		// ripple speed
#define WARP_AMPLITUDE 0.0625f	// ripple depth in texture-coordinate space
#define SKY_BACK_SCROLL_SPEED 8.0f	// sky back layer texels per second
#define SKY_FRONT_SCROLL_SPEED 16.0f	// sky cloud layer texels per second

// The "+0".."+N" animation sequence of a texture, owned by its "+0" frame
struct TextureAnim
{
	int total = 0;		// Number of frames in the sequence (0/1 = not animated)
	int frames[10];		// Texture index of each animation frame, or -1
};

// Everything derived from one BSP texture: its OpenGL objects and classification flags
struct Texture
{
	unsigned int objName = 0;			// OpenGL texture object name
	unsigned int lumaObjName = 0;		// OpenGL luma (fullbright overlay) texture object name
	unsigned int skyBackObjName = 0;	// OpenGL sky back-layer texture object name
	unsigned int skyFrontObjName = 0;	// OpenGL sky cloud-layer texture object name
	TextureAnim anim;					// "+0".."+N" animation sequence
	bool sky = false;					// True if this is a sky texture
	bool turbulent = false;				// True if this is a liquid (turbulent) texture
	bool hasLuma = false;				// True if texture has fullbright pixels
	int skyLayerWidth = 0;				// Sky layer width
	int skyLayerHeight = 0;				// Sky layer height
};

// Everything derived from one BSP surface (face): its vertices' lightmap and lighting state
struct Surface
{
	unsigned int lightmapObjName = 0;	// OpenGL lightmap texture object name
	int lightmapWidth = 0;				// Lightmap width
	int lightmapHeight = 0;				// Lightmap height
	bool lightmapDynamic = false;		// True if the lightmap has any animating styles
	int lightmapFrame = -1;				// Frame number of last lightmap rebuild
};

struct World
{
	RETRO_BSP map;							// The loaded map (BSP, palette and colormap), owned by value

	primdesc_t *surfacePrimitives = NULL;	// Array of surface primitives, contains vertex, texture and lightmap information for every surface
	Texture *textures = NULL;				// Array of per-BSP-texture OpenGL state, one per BSP texture
	Surface *surfaces = NULL;				// Array of per-surface OpenGL state, one per surface
	int *visibleSurfaces = NULL;			// Array of visible surfaces, contains an index to the surfaces
	int lightStyleFrame = -1;				// Current frame index of the 10Hz light animations
	int lightStyles[64];					// Current values of the 64 light styles
	double lightStyleTime = 0.0;			// Time accumulator for light styles
	int numMaxEdgesPerSurface = 0;			// Max edges per surface
	int numTextures = 0;					// Number of OpenGL texture objects
	int skyTextureIndex = -1;				// BSP texture used for the continuous sky background
	double textureTime = 0.0;				// Accumulated time driving texture animation
};

World world;
RETRO_Camera camera;

static unsigned int PaletteRGBA(World *world, unsigned char color, unsigned char alpha = 255)
{
	return (world->map.palette[color] & 0x00ffffff) | ((unsigned int)alpha << 24);
}

//
// True if the texture name begins with "sky" (case-insensitive)
//
bool IsSkyTextureName(const char *name)
{
	if (!name) {
		return false;
	}
	char c0 = name[0];
	char c1 = name[1];
	char c2 = name[2];
	if (c0 >= 'A' && c0 <= 'Z') c0 += 'a' - 'A';
	if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
	if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
	return c0 == 's' && c1 == 'k' && c2 == 'y';
}

//
// True if the texture is a liquid (Quake names liquids "*...")
//
bool IsTurbulentTextureName(const char *name)
{
	return name && name[0] == '*';
}

void UploadSkyLayer(unsigned int textureObj, int width, int height, unsigned int *texture)
{
	glBindTexture(GL_TEXTURE_2D, textureObj);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, texture);
}

//
// Quake sky textures are usually 256x128: the left half is an alpha-tested cloud
// layer and the right half is the solid back layer.
//
void UploadSkyTextures(World *world, int textureIndex, miptex_t *mipTexture)
{
	Texture *texture = &world->textures[textureIndex];

	int width = mipTexture->width;
	int height = mipTexture->height;
	int layerWidth = (width >= 2) ? width / 2 : width;
	bool hasCloudLayer = (layerWidth * 2 <= width);
	int backOffset = hasCloudLayer ? layerWidth : 0;

	texture->skyLayerWidth = layerWidth;
	texture->skyLayerHeight = height;

	unsigned int *backLayer = new unsigned int [layerWidth * height];
	unsigned int *frontLayer = new unsigned int [layerWidth * height];
	unsigned char *rawTexture = (unsigned char *)mipTexture + mipTexture->offsets[0];

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < layerWidth; x++) {
			unsigned char backColor = rawTexture[(x + backOffset) + y * width];
			backLayer[x + y * layerWidth] = PaletteRGBA(world, backColor);

			unsigned char frontColor = hasCloudLayer ? rawTexture[x + y * width] : 0;
			unsigned char alpha = (hasCloudLayer && frontColor != 0) ? 255 : 0;
			frontLayer[x + y * layerWidth] = PaletteRGBA(world, frontColor, alpha);
		}
	}

	UploadSkyLayer(texture->skyBackObjName, layerWidth, height, backLayer);
	UploadSkyLayer(texture->skyFrontObjName, layerWidth, height, frontLayer);

	delete[] backLayer;
	delete[] frontLayer;
}

//
// Create one OpenGL texture object per BSP texture and upload mipmapped RGBA data
//
bool UploadTextures(World *world)
{
	world->numTextures = world->map.getNumTextures();
	world->textures = new Texture [world->numTextures];

	for (int i = 0; i < world->numTextures; i++) {
		Texture *texture = &world->textures[i];

		unsigned int names[4];
		glGenTextures(4, names);
		texture->objName = names[0];
		texture->lumaObjName = names[1];
		texture->skyBackObjName = names[2];
		texture->skyFrontObjName = names[3];

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
		unsigned int *lumaPixels = new unsigned int [width * height];
		texture->sky = IsSkyTextureName(mipTexture->name);
		texture->turbulent = IsTurbulentTextureName(mipTexture->name);

		bool hasLuma = false;
		// Point to the raw 8-bit texture data (the full-resolution mip level)
		unsigned char *rawTexture = (unsigned char *)mipTexture + mipTexture->offsets[0];
		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				unsigned char colorIndex = rawTexture[x + y * width];
				pixels[x + y * width] = PaletteRGBA(world, colorIndex);
				if (colorIndex >= 224) {
					hasLuma = true;
					lumaPixels[x + y * width] = PaletteRGBA(world, colorIndex, 255);
				} else {
					lumaPixels[x + y * width] = 0x00000000;
				}
			}
		}
		texture->hasLuma = hasLuma;

		// Create mipmaps from the created texture
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		delete[] pixels;

		if (hasLuma) {
			glBindTexture(GL_TEXTURE_2D, texture->lumaObjName);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, lumaPixels);
		}
		delete[] lumaPixels;

		if (texture->sky) {
			UploadSkyTextures(world, i, mipTexture);
			if (world->skyTextureIndex < 0) {
				world->skyTextureIndex = i;
			}
		}
	}

	return true;
}

//
// Classic Quake light-style brightness patterns. Each character 'a'..'z' is a
// brightness level ('a' = dark, 'm' = normal, 'z' = bright), cycled at 10 Hz.
//
const char *LightStylePattern(int style)
{
	static const char *patterns[] = {
		"m",
		"mmnmmommommnonmmonqnmmo",
		"abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
		"mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
		"mamamamamama",
		"jklmnopqrstuvwxyzyxwvutsrqponmlkj",
		"nmonqnmomnmomomno",
		"mmmaaaabcdefgmmmmaaaammmaamm",
		"mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
		"aaaaaaaazzzzzzzz",
		"mmamammmmammamamaaamammma",
		"abcdefghijklmnopqrrqponmlkjihgfedcba",
		"mmnnmmnnnmmnn",
		"kmjmlnklkj",
		"mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmaaaaaaaazzzzzzzz",
	};

	if (style >= 0 && style < (int)(sizeof(patterns) / sizeof(patterns[0]))) {
		return patterns[style];
	}
	return "m";
}

//
// Advance classic Quake animated light style values
//
void UpdateLightStyles(World *world, double deltaTime)
{
	world->lightStyleTime += deltaTime;
	int frame = (int)(world->lightStyleTime * 10.0);	// light styles animate at 10 Hz
	if (frame == world->lightStyleFrame) {
		return;
	}
	world->lightStyleFrame = frame;

	for (int style = 0; style < 64; style++) {
		const char *pattern = LightStylePattern(style);
		int length = 0;
		while (pattern[length]) {
			length++;
		}

		// Map the current pattern letter 'a'..'z' onto a brightness scale where
		// 'm' (== 264) is normal full-strength lighting.
		char value = pattern[length > 0 ? frame % length : 0];
		if (value < 'a') value = 'a';
		if (value > 'z') value = 'z';
		world->lightStyles[style] = ((int)value - (int)'a') * 22;
	}
}

//
// Rebuild a dynamic lightmap for the specified surface with the current light style values
//
void RebuildLightmap(World *world, int surface)
{
	Surface *surf = &world->surfaces[surface];
	int width = surf->lightmapWidth;
	int height = surf->lightmapHeight;

	glBindTexture(GL_TEXTURE_2D, surf->lightmapObjName);

	dface_t *face = world->map.getSurface(surface);
	unsigned char *samples = world->map.getLightmap(face->lightofs);

	if (!samples) {
		return;
	}

	int size = width * height;
	unsigned char *luxels = new unsigned char [size];
	for (int i = 0; i < size; i++) {
		float intensity = 0.0f;
		for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; style++) {
			int styleIndex = face->styles[style];
			float scale = 1.0f;
			if (styleIndex < 64) {
				scale = (float)world->lightStyles[styleIndex] / 264.0f;
			}
			intensity += (float)samples[style * size + i] * scale;
		}
		int intVal = (int)(intensity + 0.5f);
		luxels[i] = (intVal > 255) ? 255 : (unsigned char)intVal;
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, luxels);
	delete[] luxels;
}

//
// Frame number 0-9 of a "+N..." animated texture, or -1 if it is not animated
//
int TextureAnimationFrame(const char *name)
{
	if (!name || name[0] != '+') {
		return -1;
	}
	if (name[1] >= '0' && name[1] <= '9') {
		return name[1] - '0';
	}
	return -1;
}

//
// True if two "+" animated texture names belong to the same sequence
// (identical past the leading "+N" frame marker)
//
bool IsSameTextureAnimation(const char *a, const char *b)
{
	if (!a || !b || a[0] != '+' || b[0] != '+') {
		return false;
	}
	for (int i = 2; i < 16; i++) {
		if (a[i] != b[i]) {
			return false;
		}
		if (a[i] == '\0') {
			return true;
		}
	}
	return true;
}

//
// Collect the "+0".."+9" frames of each texture animation into its "+0" texture
//
bool BuildTextureAnimations(World *world)
{
	for (int i = 0; i < world->numTextures; i++) {
		world->textures[i].anim.total = 0;
		for (int frame = 0; frame < 10; frame++) {
			world->textures[i].anim.frames[frame] = -1;
		}
	}

	for (int i = 0; i < world->numTextures; i++) {
		miptex_t *baseTexture = world->map.getMipTexture(i);
		if (!baseTexture) {
			continue;
		}
		// Only the "+0" texture of a sequence owns the frame list
		if (TextureAnimationFrame(baseTexture->name) != 0) {
			continue;
		}

		TextureAnim *anim = &world->textures[i].anim;
		for (int j = 0; j < world->numTextures; j++) {
			miptex_t *frameTexture = world->map.getMipTexture(j);
			if (!frameTexture || !IsSameTextureAnimation(baseTexture->name, frameTexture->name)) {
				continue;
			}
			int frame = TextureAnimationFrame(frameTexture->name);
			if (frame >= 0 && frame < 10) {
				anim->frames[frame] = j;
				if (frame + 1 > anim->total) {
					anim->total = frame + 1;
				}
			}
		}
	}

	return true;
}

//
// Resolve a texture index to its current animation frame for this render time
//
int ResolveTextureAnimation(World *world, int textureIndex)
{
	TextureAnim *anim = &world->textures[textureIndex].anim;
	if (anim->total <= 1) {
		return textureIndex;
	}

	// Textures animate at 10 frames per second
	int frame = ((int)(world->textureTime * 10.0)) % anim->total;
	int resolved = anim->frames[frame];
	if (resolved < 0) {
		resolved = anim->frames[0];
	}
	if (resolved < 0 || resolved >= world->numTextures) {
		return textureIndex;
	}
	return resolved;
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

		world->surfaces[i].lightmapWidth = lightWidth;
		world->surfaces[i].lightmapHeight = lightHeight;

		// Special (sky/liquid) surfaces always get BuildLightmap's 1x1 white texel,
		// regardless of styles, so they must never be treated as dynamic.
		dface_t *face = world->map.getSurface(i);
		bool dynamic = false;
		if (!(textureInfo->flags & TEX_SPECIAL)) {
			for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; style++) {
				if (face->styles[style] > 0 && face->styles[style] < 64) {
					dynamic = true;
					break;
				}
			}
		}
		world->surfaces[i].lightmapDynamic = dynamic;

		// Create the lightmap texture for this surface
		BuildLightmap(world, i, lightWidth, lightHeight);
	}

	return true;
}

void SkyTexCoord(World *world, int textureIndex, const float dir[3], float scroll, float *s, float *t)
{
	float skyDir[3] = {
		dir[0],
		dir[1],
		dir[2] * 3.0f
	};
	float length = sqrtf(skyDir[0] * skyDir[0] + skyDir[1] * skyDir[1] + skyDir[2] * skyDir[2]);
	if (length < 0.0001f) {
		length = 0.0001f;
	}

	Texture *texture = &world->textures[textureIndex];
	int width = texture->skyLayerWidth > 0 ? texture->skyLayerWidth : 128;
	int height = texture->skyLayerHeight > 0 ? texture->skyLayerHeight : 128;
	float scale = (6.0f * 63.0f) / length;
	*s = (scroll + skyDir[0] * scale) / (float)width;
	*t = (scroll + skyDir[1] * scale) / (float)height;
}

void DrawSkyBackgroundLayer(World *world, RETRO_Camera *camera, int textureIndex, unsigned int textureObj, float scroll)
{
	const int slices = 64;
	const int stacks = 32;
	const float radius = 2048.0f;

	glBindTexture(GL_TEXTURE_2D, textureObj);

	for (int stack = 0; stack < stacks; stack++) {
		float phi0 = (-0.5f + (float)stack / (float)stacks) * (float)M_PI;
		float phi1 = (-0.5f + (float)(stack + 1) / (float)stacks) * (float)M_PI;

		glBegin(GL_QUAD_STRIP);
		for (int slice = 0; slice <= slices; slice++) {
			float theta = ((float)slice / (float)slices) * 2.0f * (float)M_PI;
			float cosTheta = cosf(theta);
			float sinTheta = sinf(theta);

			float dir0[3] = { cosf(phi0) * cosTheta, cosf(phi0) * sinTheta, sinf(phi0) };
			float dir1[3] = { cosf(phi1) * cosTheta, cosf(phi1) * sinTheta, sinf(phi1) };
			float s, t;

			SkyTexCoord(world, textureIndex, dir1, scroll, &s, &t);
			glTexCoord2f(s, t);
			glVertex3f(camera->origin[0] + dir1[0] * radius,
					camera->origin[1] + dir1[1] * radius,
					camera->origin[2] + dir1[2] * radius);

			SkyTexCoord(world, textureIndex, dir0, scroll, &s, &t);
			glTexCoord2f(s, t);
			glVertex3f(camera->origin[0] + dir0[0] * radius,
					camera->origin[1] + dir0[1] * radius,
					camera->origin[2] + dir0[2] * radius);
		}
		glEnd();
	}
}

void DrawSkyBackground(World *world, RETRO_Camera *camera)
{
	int textureIndex = world->skyTextureIndex;
	if (textureIndex < 0 || textureIndex >= world->numTextures || world->textures[textureIndex].skyBackObjName == 0) {
		return;
	}

	glActiveTextureFn(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureFn(GL_TEXTURE0);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glDisable(GL_BLEND);
	DrawSkyBackgroundLayer(world, camera, textureIndex, world->textures[textureIndex].skyBackObjName,
			(float)(world->textureTime * SKY_BACK_SCROLL_SPEED));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	DrawSkyBackgroundLayer(world, camera, textureIndex, world->textures[textureIndex].skyFrontObjName,
			(float)(world->textureTime * SKY_FRONT_SCROLL_SPEED));
	glDisable(GL_BLEND);

	glEnable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glActiveTextureFn(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glActiveTextureFn(GL_TEXTURE0);
}

//
// Draw the surface
//
void DrawSurface(World *world, int surface)
{
	// Get the surface primitive
	primdesc_t *primitives = &world->surfacePrimitives[world->numMaxEdgesPerSurface * surface];

	// Liquid surfaces ripple their texture coordinates.
	int miptex = world->map.getTextureInfo(surface)->miptex;
	bool turbulent = world->textures[miptex].turbulent;
	float time = (float)world->textureTime;

	// Loop through all vertices of the primitive and draw a surface. BSP faces are
	// convex, so a triangle fan from the first vertex fills the whole face.
	glBegin(GL_TRIANGLE_FAN);
	for (int i = 0; i < world->map.getNumEdges(surface); i++, primitives++) {
		float s = primitives->t[0];
		float t = primitives->t[1];
		if (turbulent) {
			// Ripple the texture coordinates with a time-varying sine
			s = primitives->t[0] + sinf(primitives->t[1] * WARP_SPACE_FREQ + time * WARP_TIME_FREQ) * WARP_AMPLITUDE;
			t = primitives->t[1] + sinf(primitives->t[0] * WARP_SPACE_FREQ + time * WARP_TIME_FREQ) * WARP_AMPLITUDE;
		}
		glMultiTexCoord2fFn(GL_TEXTURE0, s, t);
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
		int surfaceIndex = visibleSurfaces[i];
		Surface *surface = &world->surfaces[surfaceIndex];
		// If the lightmap is dynamic, rebuild it with the current style values
		if (surface->lightmapDynamic && surface->lightmapFrame != world->lightStyleFrame) {
			RebuildLightmap(world, surfaceIndex);
			surface->lightmapFrame = world->lightStyleFrame;
		}
		// Get a pointer to the texture info so we know which base texture to bind
		texinfo_t *textureInfo = world->map.getTextureInfo(surfaceIndex);
		// Resolve animated textures to their current frame
		int textureIndex = ResolveTextureAnimation(world, textureInfo->miptex);
		Texture *texture = &world->textures[textureIndex];
		if (texture->sky) {
			continue;
		}
		// Bind the base texture to unit 0 and the surface's lightmap to unit 1
		glActiveTextureFn(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture->objName);
		glActiveTextureFn(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, surface->lightmapObjName);
		// Draw the surface
		DrawSurface(world, surfaceIndex);

		// If the texture has luma/fullbright pixels, draw a second pass
		if (texture->hasLuma) {
			// Disable multitexturing
			glActiveTextureFn(GL_TEXTURE1);
			glDisable(GL_TEXTURE_2D);

			// Enable alpha test
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.0f);

			// Bind luma texture to unit 0
			glActiveTextureFn(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texture->lumaObjName);

			// Draw the surface again
			DrawSurface(world, surfaceIndex);

			// Restore state
			glDisable(GL_ALPHA_TEST);
			glActiveTextureFn(GL_TEXTURE1);
			glEnable(GL_TEXTURE_2D);
			glActiveTextureFn(GL_TEXTURE0);
		}
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
	if (!BuildTextureAnimations(&world)) {
		RETRO_RageQuit("Unable to initialize texture animations\n");
	}

	UpdateLightStyles(&world, 0.0);

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
			Texture *texture = &world.textures[i];
			glDeleteTextures(1, &texture->objName);
			glDeleteTextures(1, &texture->lumaObjName);
			glDeleteTextures(1, &texture->skyBackObjName);
			glDeleteTextures(1, &texture->skyFrontObjName);
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
	if (world.surfacePrimitives) { delete[] world.surfacePrimitives; world.surfacePrimitives = NULL; }
	if (world.visibleSurfaces) { delete[] world.visibleSurfaces; world.visibleSurfaces = NULL; }
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

	// Advance the clock that drives texture animation
	world.textureTime += deltatime;

	// Advance the clock that drives light style animation
	UpdateLightStyles(&world, deltatime);

	// Draw one continuous sky behind the world; BSP sky faces are skipped so they
	// reveal this background instead of carrying their own texture projection.
	DrawSkyBackground(&world, &camera);

	// Find the leaf the camera is in
	dleaf_t *leaf = FindCameraLeaf(&world, &camera);

	// Render the scene
	DrawVisibleSet(&world, leaf);
}
