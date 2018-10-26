//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#ifndef _CAMERA_H_
#define _CAMERA_H_

typedef float vec3_t[3];

class Camera
{
private:
	float yaw;			// Direction of travel
	float pitch;		// Neck angle
	float speed;		// Speed along heading
	float strafe;		// Speed along heading

public:
	vec3_t head;		// Position of head
	vec3_t view;		// Normal along viewing direction

	Camera();
	void UpdatePosition(void);
	void Pitch(float degrees);
	void Yaw(float degrees);
	void PitchUp(void);
	void PitchDown(void);
	void MoveForward(void);
	void MoveBackward(void);
	void TurnRight(void);
	void TurnLeft(void);
	void StrafeRight(void);
	void StrafeLeft(void);
};

#endif
