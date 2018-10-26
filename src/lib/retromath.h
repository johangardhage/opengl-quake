//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//

#ifndef _RETROMATH_H_
#define _RETROMATH_H_

#include <math.h>

// Dot product of two 3D vectors
inline float DotProduct(const float a[3], const float b[3])
{
	return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
}

// Normalize a 3D vector in place (leaves a zero-length vector untouched)
inline void Normalize(float v[3])
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length == 0.0f) {
		return;
	}
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

// Cross product: result = a x b
inline void Cross(const float a[3], const float b[3], float result[3])
{
	result[0] = a[1] * b[2] - a[2] * b[1];
	result[1] = a[2] * b[0] - a[0] * b[2];
	result[2] = a[0] * b[1] - a[1] * b[0];
}

// Signed area of edge a->b versus point (x,y). Its sign says which side of the edge
// the point is on; used for triangle coverage tests and barycentric weights.
inline float EdgeFunction(float ax, float ay, float bx, float by, float x, float y)
{
	return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}

// Wrap a texel coordinate into [0, size), handling negative values
inline int WrapTexel(int value, int size)
{
	value %= size;
	if (value < 0) {
		value += size;
	}
	return value;
}

// Floor of value/16; lightmap luxels are spaced one per 16 texels
inline int FloorDiv16(float value)
{
	return (int)floorf(value / 16.0f);
}

// Ceil of value/16; lightmap luxels are spaced one per 16 texels
inline int CeilDiv16(float value)
{
	return (int)ceilf(value / 16.0f);
}

#endif
