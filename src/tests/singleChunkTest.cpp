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
#include "ChunkMesher.h"

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

    ResourceController resourceController= ResourceController;
    Camera camera = Camera(width, height, glm::vec3(0, 2, -10));
    Renderer renderer = Renderer(&camera);


    //define the chunkMesher
    ChunkMesher mesher;

    //test chunk to try and do things to
    Voxel air;
    air.ID = 0;
    air.voxelLayer = 0;
    Voxel brick = Voxel();
    brick.ID = 1;
    brick.voxelLayer = 1;
    brick.voxelType = 0;

    Chunk chunk(brick);
    chunk.SetVoxel(0,0,0, air);
    chunk.SetVoxel(0,0,1, air);
    chunk.SetVoxel(0,0,2, air);
    chunk.SetVoxel(1,0,0, air);
    chunk.SetVoxel(1,0,2, air);
    chunk.SetVoxel(2,0,0, air);
    chunk.SetVoxel(2,0,1, air);
    chunk.SetVoxel(2,0,2, air);


    chunk.SetVoxel(0,0,31, air);
    chunk.SetVoxel(0,31,0, air);
    chunk.SetVoxel(0,31,31, air);
    chunk.SetVoxel(31,0,0, air);
    chunk.SetVoxel(31,0,31, air);
    chunk.SetVoxel(31,31,0, air);
    chunk.SetVoxel(31,31,31, air);



    ChunkMeshData meshData;
    mesher.MeshChunk(chunk, meshData.vertices, meshData.indices, meshData.triangleTextures);

    renderer.AddChunk(chunk.position, meshData);



    bool loaded;
    Entity max = resourceController.LoadEntity("maxSternum", loaded);
    renderer.AddEntity(max);


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
    //double delta   = currentTime - lastFrameTime;
    lastFrameTime     = currentTime;

    while (!glfwWindowShouldClose(window))
    {
        currentTime = glfwGetTime();
        //delta   = currentTime - lastFrameTime;
        lastFrameTime     = currentTime;
        glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.Inputs(window);

        renderer.UpdateEntity(max);

        renderer.Render();

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

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}