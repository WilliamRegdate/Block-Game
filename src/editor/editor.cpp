#include <iostream>
#include <vector>
#include <memory>
#include <random>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include "MeshLoader.h"
#include "MaterialLoader.h"
#include "Renderer.h"
#include "ResourceController.h"
#include "EntityEditor.h"

const unsigned int width  = 1920;
const unsigned int height = 1080;


int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Block Game Render Test", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // vsync off, otherwise frame time is capped at the monitor refresh rate
    gladLoadGL();
    glViewport(0, 0, width, height);


    Camera camera = Camera(width, height, glm::vec3(0, 1, -4));
    Renderer renderer = Renderer(&camera);
    ResourceEditor resourceController = ResourceEditor(renderer);

    // Entity editor (F1). Created after the renderer, destroyed before the window.
    auto editor = std::make_unique<EntityEditor>(window, resourceController, renderer);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---- Frame timing ----
    double lastReport = glfwGetTime();
    int    frames     = 0;

    GLuint q; glGenQueries(1, &q);  // once, before the loop

    glBeginQuery(GL_TIME_ELAPSED, q);
    glEndQuery(GL_TIME_ELAPSED);

    GLuint64 ns = 0;
    glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns); // stalls the CPU, fine for diagnosis
    std::cout << "GPU: " << ns / 1.0e6 << " ms\n";

    double lastFrameTime = 0;
    double currentTime = glfwGetTime();
    double delta   = currentTime - lastFrameTime;
    lastFrameTime     = currentTime;

    while (!glfwWindowShouldClose(window))
    {
        currentTime = glfwGetTime();
        delta   = currentTime - lastFrameTime;
        lastFrameTime     = currentTime;
        glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // F1 toggles the editor; while it is open the cursor belongs to the UI,
        // so the camera must not re-centre it every frame.
        editor->Update(delta);
        if (!editor->IsActive())
            camera.Inputs(window, delta);

        renderer.Render();
        editor->Render();   // draws on top of the scene, before the buffer swap

        glfwSwapBuffers(window);
        glfwPollEvents();

        ++frames;
        const double now = glfwGetTime();
        if (now - lastReport >= 1.0)
        {
            std::cout << frames << " fps (" << 1000.0 * (now - lastReport) / frames << " ms/frame)" << std::endl;
            frames = 0;
            lastReport = now;
        }
        GLenum error = glGetError();

    if (error != GL_NO_ERROR)
        std::cerr << "OpenGL error: " << error << '\n';
    }

    editor.reset();   // ImGui shutdown needs the GL context and window to still exist

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}