#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H

#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glm/gtx/rotate_vector.hpp>
#include<glm/gtx/vector_angle.hpp>

class Camera
{
    public:
		const float baseSpeed = 10.0f; 
		float speed = baseSpeed; 
        glm::vec3 position;
	    glm::vec3 orientation = glm::vec3(0.0f, 0.0f, 1.0f);
	    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	    glm::mat4 cameraMatrix = glm::mat4(1.0f);

	    // Prevents the camera from jumping around when first clicking left click
	    bool firstMove = true;

	    // Stores the width and height of the window
	    int width;
	    int height;

	    // Adjust the speed of the camera and it's sensitivity when looking around
	    float sensitivity = 100.0f;

	    // Camera constructor to set up initial values
	    Camera(int width, int height, glm::vec3 position);

	    // Updates the camera matrix
	    void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
	    // Handles camera inputs
	    void Inputs(GLFWwindow* window, float delta);
};


#endif