//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#include <math.h> // cos, sin, fabs, M_PI
#include "Camera.h"

#define WALKING_SPEED		5.0
#define TURN_SPEED			3.0
#define PITCH_SPEED			1.0
#define STRAFE_SPEED		5.0
#define MOUSE_SENSITIVITY	0.3

//
// Set the initial Camera position
//
Camera::Camera()
{
	yaw = 90;
	pitch = 0;
	head[0] = 540;
	head[1] = 260;
	head[2] = 100;
	speed = 0;
	strafe = 0;
}

//
// Update the camera position
//
void Camera::UpdatePosition(void)
{
	// Move the camera forward
	if ((fabs(speed) > 0)) {
		head[0] += speed * cos(yaw * M_PI / 180.0);
		head[1] += speed * sin(yaw * M_PI / 180.0);
		head[2] += speed * sin(pitch * M_PI / 180.0);
	}

	// Move the camera sideways
	if ((fabs(strafe) > 0)) {
		head[0] += strafe * sin(yaw * M_PI / 180.0);
		head[1] -= strafe * cos(yaw * M_PI / 180.0);
	}

	// Setup the view vector
	view[0] = cos(yaw * M_PI / 180.0);
	view[1] = sin(yaw * M_PI / 180.0);
	view[2] = sin(pitch * M_PI / 180.0);

	// Reset speed
	speed = 0;
	strafe = 0;
}

void Camera::Pitch(float degrees)
{
	pitch -= degrees * MOUSE_SENSITIVITY;
	if (pitch > 90.0f) {
		pitch = 90.0f;
	} else if (pitch < -90.0f) {
		pitch = -90.0f;
	}
}

void Camera::Yaw(float degrees)
{
	yaw -= degrees * MOUSE_SENSITIVITY;
	if (yaw < 0.0f) {
		yaw += 360.0f;
	} else if (yaw > 360.0f) {
		yaw -= 360.0f;
	}
}

void Camera::PitchUp(void)
{
	if (pitch < 90.0f) {
		pitch += PITCH_SPEED;
	}
}

void Camera::PitchDown(void)
{
	if (pitch > -90.0f) {
		pitch -= PITCH_SPEED;
	}
}

void Camera::MoveForward(void)
{
	speed += WALKING_SPEED;
}

void Camera::MoveBackward(void)
{
	speed -= WALKING_SPEED;
}

void Camera::TurnRight(void)
{
	yaw -= TURN_SPEED;
	if (yaw < 180.0f) {
		yaw += 360.0f;
	}
}

void Camera::TurnLeft(void)
{
	yaw += TURN_SPEED;
	if (yaw > 180.0f) {
		yaw -= 360.0f;
	}
}

void Camera::StrafeRight(void)
{
	strafe += STRAFE_SPEED;
}

void Camera::StrafeLeft(void)
{
	strafe -= STRAFE_SPEED;
}
