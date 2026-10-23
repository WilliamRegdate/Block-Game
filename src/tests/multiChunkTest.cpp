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

#include "Renderer.h"
#include "ResourceController.h"
#include "WorldManager.h"
#include "Collision.h"

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

    Camera camera = Camera(width, height, glm::vec3(0, 60, -10));
    Renderer renderer = Renderer(&camera);
    ResourceController resourceController(renderer);


    WorldManager wm = WorldManager(renderer, resourceController);
    CollisionDetector collision(&wm);

    bool loaded;
    Entity max = resourceController.LoadEntity("maxSternum", loaded);
    max.transform.position = glm::vec3(0, 70, 0);
    renderer.AddInstance(max.instance);

    Entity methane = resourceController.LoadEntity("methaneMonster", loaded);
    methane.transform.position = glm::vec3(5, 75, -10);
    methane.instance.animationPlayer.Play("walk");
    renderer.AddInstance(methane.instance);


    glEnable(GL_CULL_FACE);
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

    float dayLengthSeconds = 300.0f;   // one full day = 5 minutes
    float timeOfDay = 0.25f;           // start at noon

    bool mouseDown = false;
    while (!glfwWindowShouldClose(window))
    {

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS)
		{
            if (!mouseDown)
            {
                mouseDown = true;
                if (collision.GetRay(camera.position, camera.orientation, 50.0f))
                {
                    for( auto& element : collision.contactCache)
                    {
                        //break block
                        if (const BlockRef* b = std::get_if<BlockRef>(&element.target))
                        {
                            glm::ivec3 pos = b->pos;
                            //equal to setblock at coords to air
                            wm.SetBlockAt(pos, Voxel());
                        }
                    }
                }
            }
		}
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2) == GLFW_PRESS) //place block function
		{
            if (!mouseDown)
            {
                mouseDown = true;
                if (collision.GetRay(camera.position, camera.orientation, 50.0f))
                {
                    for( auto& element : collision.contactCache)
                    {
                        if (const BlockRef* b = std::get_if<BlockRef>(&element.target))
                        {
                            glm::ivec3 pos = b->pos + glm::ivec3(element.normal);
                            //equal to setblock at coords to stone
                            wm.SetBlockAt(pos, Voxel{0, 1, 1});
                        }
                    }
                }
            }
		}
        else
        {
            mouseDown = false;
        }

        timeOfDay += delta / dayLengthSeconds;
        timeOfDay -= std::floor(timeOfDay);   // keep it in 0..1 (SetTimeOfDay also wraps it)
        renderer.SetTimeOfDay(timeOfDay);

        glm::ivec3 centerChunk = glm::ivec3(glm::floor(camera.position / 32.0f));

        methane.instance.animationPlayer.Update(delta);
        wm.Update(centerChunk);

        currentTime = glfwGetTime();
        delta = currentTime - lastFrameTime;
        lastFrameTime     = currentTime;
        glClearColor(0.45f, 0.68f, 0.83f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.Inputs(window, float(delta));

        renderer.UpdateInstance(max.instance);
        renderer.UpdateInstance(methane.instance);

        renderer.Render();

        glfwSwapBuffers(window);
        glfwPollEvents();

        ++frames;
        const double now = glfwGetTime();
        // if (now - lastReport >= 1.0)
        // {
        //     std::cout << frames << " fps (" << 1000.0 * (now - lastReport) / frames << " ms/frame)" << std::endl;
        //     frames = 0;
        //     lastReport = now;
        // }
        GLenum error = glGetError();

    if (error != GL_NO_ERROR)
        std::cerr << "OpenGL error: " << error << '\n';
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}