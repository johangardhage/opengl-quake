//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//

#ifndef _RETROCAMERA_H_
#define _RETROCAMERA_H_

#include <math.h> // cosf, sinf, sqrtf, fmodf, M_PI

typedef float vec3_t[3];

#define CAMERA_TURN_SPEED        3.0
#define CAMERA_PITCH_SPEED       1.0
#define CAMERA_MOUSE_SENSITIVITY 0.3
#define CAMERA_MAX_PITCH         89.0

struct RETRO_Camera
{
	vec3_t origin;  // Position of eye
	vec3_t forward; // Forward (look) vector
	vec3_t up;      // Up vector (Quake is Z-up)

	float yaw;              // Direction of travel (degrees)
	float pitch;            // Neck angle (degrees)
	float forwardMove;      // Accumulated forward/backward movement
	float strafeMove;       // Accumulated sideways movement
	float verticalMove;     // Accumulated vertical movement
	float turnSpeed;        // Keyboard yaw increment per key tick
	float pitchSpeed;       // Keyboard pitch increment per key tick
	float mouseSensitivity; // Mouse look degrees per pixel
	float movementSpeed;    // Movement increment per key tick
	bool flycam;            // Move forward/backward along look vector

	RETRO_Camera()
	{
		origin[0] = 0.0f;
		origin[1] = 0.0f;
		origin[2] = 0.0f;
		yaw = 0.0f;
		pitch = 0.0f;
		forwardMove = 0.0f;
		strafeMove = 0.0f;
		verticalMove = 0.0f;
		turnSpeed = CAMERA_TURN_SPEED;
		pitchSpeed = CAMERA_PITCH_SPEED;
		mouseSensitivity = CAMERA_MOUSE_SENSITIVITY;
		movementSpeed = 1.0f;
		flycam = false;
		forward[0] = 1.0f;
		forward[1] = 0.0f;
		forward[2] = 0.0f;
		up[0] = 0.0f;
		up[1] = 0.0f;
		up[2] = 1.0f;
	}

	void SetPosition(float x, float y, float z)
	{
		origin[0] = x;
		origin[1] = y;
		origin[2] = z;
	}

	void SetOrientation(float yawDegrees, float pitchDegrees)
	{
		yaw = yawDegrees;
		pitch = pitchDegrees;
	}

	void SetMovementSpeed(float speed)
	{
		movementSpeed = speed;
	}

	void SetFlycam(bool enabled)
	{
		flycam = enabled;
	}

	void MoveForward(float scale = 1.0f)
	{
		forwardMove += movementSpeed * scale;
	}

	void MoveBackward(float scale = 1.0f)
	{
		forwardMove -= movementSpeed * scale;
	}

	void StrafeRight(float scale = 1.0f)
	{
		strafeMove += movementSpeed * scale;
	}

	void StrafeLeft(float scale = 1.0f)
	{
		strafeMove -= movementSpeed * scale;
	}

	void MoveUp(float scale = 1.0f)
	{
		verticalMove += movementSpeed * scale;
	}

	void MoveDown(float scale = 1.0f)
	{
		verticalMove -= movementSpeed * scale;
	}

	void TurnRight(float scale = 1.0f)
	{
		yaw -= turnSpeed * scale;
	}

	void TurnLeft(float scale = 1.0f)
	{
		yaw += turnSpeed * scale;
	}

	void PitchUp(float scale = 1.0f)
	{
		pitch += pitchSpeed * scale;
	}

	void PitchDown(float scale = 1.0f)
	{
		pitch -= pitchSpeed * scale;
	}

	void MouseLook(float xrel, float yrel)
	{
		yaw -= xrel * mouseSensitivity;
		pitch -= yrel * mouseSensitivity;
	}

	void Update(void)
	{
		// Keep yaw circular, but clamp pitch before it reaches the fixed Z-up vector.
		yaw = fmodf(yaw, 360.0f);
		if (yaw < 0.0f) {
			yaw += 360.0f;
		}
		if (pitch > CAMERA_MAX_PITCH) {
			pitch = CAMERA_MAX_PITCH;
		} else if (pitch < -CAMERA_MAX_PITCH) {
			pitch = -CAMERA_MAX_PITCH;
		}

		float yawRadians = yaw * (float)M_PI / 180.0f;
		float pitchRadians = pitch * (float)M_PI / 180.0f;
		float cosPitch = cosf(pitchRadians);

		// Spherical coordinates: yaw turns around Z, pitch tilts above/below XY.
		forward[0] = cosPitch * cosf(yawRadians);
		forward[1] = cosPitch * sinf(yawRadians);
		forward[2] = sinf(pitchRadians);

		// Walk mode uses horizontal forward movement; flycam follows the look vector.
		float heading[3] = { cosf(yawRadians), sinf(yawRadians), 0.0f };
		float right[3] = { sinf(yawRadians), -cosf(yawRadians), 0.0f };
		float cameraUp[3] = {
			right[1] * forward[2] - right[2] * forward[1],
			right[2] * forward[0] - right[0] * forward[2],
			right[0] * forward[1] - right[1] * forward[0]
		};

		float move[3] = {
			forwardMove * (flycam ? forward[0] : heading[0]) + strafeMove * right[0],
			forwardMove * (flycam ? forward[1] : heading[1]) + strafeMove * right[1],
			forwardMove * (flycam ? forward[2] : heading[2]) + strafeMove * right[2]
		};

		if (flycam) {
			move[0] += verticalMove * cameraUp[0];
			move[1] += verticalMove * cameraUp[1];
			move[2] += verticalMove * cameraUp[2];
		} else {
			move[2] += verticalMove;
		}

		// Normalize the combined command so diagonals are not faster.
		float length = sqrtf(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
		if (length > 0.0f) {
			float maxInput = fabsf(forwardMove);
			if (fabsf(strafeMove) > maxInput) {
				maxInput = fabsf(strafeMove);
			}
			if (fabsf(verticalMove) > maxInput) {
				maxInput = fabsf(verticalMove);
			}
			origin[0] += (move[0] / length) * maxInput;
			origin[1] += (move[1] / length) * maxInput;
			origin[2] += (move[2] / length) * maxInput;
		}

		forwardMove = 0.0f;
		strafeMove = 0.0f;
		verticalMove = 0.0f;
	}
};

#endif
