//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#include <fcntl.h> // open
#include <stdio.h> // printf
#include <stdlib.h> // malloc, free
#include <sys/stat.h> // stat
#include <unistd.h> // lseek, read, close
#include "Map.h"

bool Map::Initialize(const char *bspFilename, const char *paletteFilename)
{
	if (!LoadBSP(bspFilename)) {
		printf("[ERROR] Map::Initialize() Error loading bsp file\n");
		return false;
	}

	if (!LoadPalette(paletteFilename)) {
		printf("[ERROR] Map::Initialize() Error loading palette\n");
		return false;
	}

	return true;
}

int Map::LoadFile(const char *filename, void **bufferptr)
{
	struct stat st;
	void *buffer;
	int length;

	int f = open(filename, O_RDONLY);
	if (f == -1) {
		return 0;
	}

	stat(filename, &st);
	length = st.st_size;
	lseek(f, 0, SEEK_SET);
	buffer = malloc(length);
	read(f, buffer, length);
	close(f);

	*bufferptr = buffer;
	return length;
}

bool Map::LoadPalette(const char *filename)
{
	unsigned char *tempPal;

	if (!LoadFile(filename, (void **)&tempPal)) {
		return false;
	}

	for (int i = 0; i < 256; i++) {
		unsigned int r = tempPal[i * 3 + 0];
		unsigned int g = tempPal[i * 3 + 1];
		unsigned int b = tempPal[i * 3 + 2];
		palette[i] = (r) | (g << 8) | (b << 16);
	}

	free(tempPal);
	return true;
}

bool Map::LoadBSP(const char *filename)
{
	if (!LoadFile(filename, (void **)&bsp)) {
		return false;
	}

	header = (dheader_t *)bsp;
	if (header->version != BSP_VERSION) {
		printf("[ERROR] Map::LoadBSP() BSP file version mismatch!\n");
		return false;
	}

	return true;
}
