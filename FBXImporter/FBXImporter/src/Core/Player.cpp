#include "Player.h"
#include "../Core/Audio.hpp"
#include "../Core/Input.h"
#include "../Core/GL.h"
#include "../Common.h"

namespace Player {
	glm::vec3 _position = glm::vec3(0);
	glm::vec3 _rotation = glm::vec3(-0.1f, -HELL_PI * 0.5f, 0);
	float _viewHeightStanding = 1.65f;
	float _viewHeightCrouching = 1.15f;
	//float _viewHeightCrouching = 3.15f; hovery
	float _crouchDownSpeed = 17.5f;
	float _currentViewHeight = _viewHeightStanding;
	float _walkingSpeed = 4;
	float _crouchingSpeed = 2;
	glm::mat4 _viewMatrix = glm::mat4(1);
	glm::mat4 _inverseViewMatrix = glm::mat4(1);
	glm::vec3 _viewPos = glm::vec3(0);
	glm::vec3 _front = glm::vec3(0);
	glm::vec3 _forward = glm::vec3(0);
	glm::vec3 _up = glm::vec3(0);
	glm::vec3 _right = glm::vec3(0);
	float _breatheAmplitude = 0.00052f;
	float _breatheFrequency = 8;
	float _headBobAmplitude = 0.00505f;
	float _headBobFrequency = 25.0f;
}

inline float FInterpTo(float current, float target, float deltaTime, float interpSpeed) {
	// If no interp speed, jump to target value
	if (interpSpeed <= 0.f)
		return target;
	// Distance to reach
	const float Dist = target - current;
	// If distance is too small, just set the desired location
	if (Dist * Dist < SMALL_NUMBER)
		return target;
	// Delta Move, Clamp so we do not over shoot.
	const float DeltaMove = Dist * glm::clamp(deltaTime * interpSpeed, 0.0f, 1.0f);
	return current + DeltaMove;
}


void Player::Init(glm::vec3 position) {
	_position = position;
}

void Player::Update(float deltaTime) {
	
	// Mouselook
	if (GL::WindowHasFocus()) {
		float mouseSensitivity = 0.002f;
		static float targetXRot = _rotation.x;
		static float targetYRot = _rotation.y;
		targetXRot += -Input::GetMouseOffsetY() * mouseSensitivity;
		targetYRot += -Input::GetMouseOffsetX() * mouseSensitivity;
		float cameraRotateSpeed = 50;
		_rotation.x = FInterpTo(_rotation.x, targetXRot, deltaTime, cameraRotateSpeed);
		_rotation.y = FInterpTo(_rotation.y, targetYRot, deltaTime, cameraRotateSpeed);
		_rotation.x = std::min(_rotation.x, 1.5f);
		_rotation.x = std::max(_rotation.x, -1.5f);
	}

	// Crouching
	bool crouching = false;
	if (Input::KeyDown(HELL_KEY_LEFT_CONTROL_GLFW)) {
		crouching = true;
	}

	// Speed
	float speed = crouching ? _crouchingSpeed : _walkingSpeed;
	speed *= deltaTime;

	// View height
	float viewHeightTarget = crouching ? _viewHeightCrouching : _viewHeightStanding;
	_currentViewHeight = FInterpTo(_currentViewHeight, viewHeightTarget, deltaTime, _crouchDownSpeed);

	// Breathe bob
	static float totalTime;
	totalTime += 0.0075f;
	Transform breatheTransform;
	breatheTransform.position.x = cos(totalTime * _breatheFrequency) * _breatheAmplitude * 1;
	breatheTransform.position.y = sin(totalTime * _breatheFrequency) * _breatheAmplitude * 2;

	// Head bob
	Transform headBobTransform;
	static bool moving = false;
	if (moving) {
		headBobTransform.position.x = cos(totalTime * _headBobFrequency) * _headBobAmplitude * 1;
		headBobTransform.position.y = sin(totalTime * _headBobFrequency) * _headBobAmplitude * 2;
	}

	// View matrix
	Transform camTransform;
	camTransform.position = _position + glm::vec3(0, _currentViewHeight, 0);
	camTransform.rotation = _rotation;
	_viewMatrix = glm::inverse(headBobTransform.to_mat4() * breatheTransform.to_mat4() * camTransform.to_mat4());
	_inverseViewMatrix = glm::inverse(_viewMatrix);
	_right = glm::vec3(_inverseViewMatrix[0]);
	_up = glm::vec3(_inverseViewMatrix[1]);
	_front = glm::vec3(_inverseViewMatrix[2]);// *glm::vec3(-1, -1, -1);
	_forward = glm::normalize(glm::vec3(_front.x, 0, _front.z));
	_viewPos = _inverseViewMatrix[3];

	// WSAD movement
	glm::vec3 displacement(0); 
	moving = false;
	if (Input::KeyDown(HELL_KEY_W)) {
		displacement -= _forward * speed;
		moving = true;
	}
	if (Input::KeyDown(HELL_KEY_S)) {
		displacement += _forward * speed;
		moving = true;
	}
	if (Input::KeyDown(HELL_KEY_A)) {
		displacement -= _right * speed;
		moving = true;
	}
	if (Input::KeyDown(HELL_KEY_D)) {
		displacement += _right * speed;
		moving = true;
	}
	_position += displacement;

	// Footstep audio
	static float m_footstepAudioTimer = 0;
	static float footstepAudioLoopLength = 0.5;

	if (!moving)
		m_footstepAudioTimer = 0;
	else
	{
		if (moving && m_footstepAudioTimer == 0) {
			int random_number = std::rand() % 4 + 1;
			std::string file = "player_step_" + std::to_string(random_number) + ".wav";
			Audio::PlayAudio(file.c_str(), 0.5f);
		}
		float timerIncrement = crouching ? deltaTime * 0.75f : deltaTime;
		m_footstepAudioTimer += timerIncrement;

		if (m_footstepAudioTimer > footstepAudioLoopLength)
			m_footstepAudioTimer = 0;
	}
}

glm::mat4 Player::GetViewMatrix() {
	return _viewMatrix;
}

glm::mat4 Player::GetInverseViewMatrix() {
	return _inverseViewMatrix;
}

glm::vec3 Player::GetViewPos() {
	return _viewPos;
}

glm::vec3 Player::GetViewRotation() {
	return _rotation;
}


glm::vec3 Player::GetFeetPosition() {
	return _position;
}

glm::vec3 Player::GetCameraRight() {
	return _right;
}

glm::vec3 Player::GetCameraFront() {
	return _front;
}

glm::vec3 Player::GetCameraUp() {
	return _up;
}
