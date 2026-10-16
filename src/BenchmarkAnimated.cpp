#include <iostream>
#include <vector>
#include <list>
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

const unsigned int width  = 1920;
const unsigned int height = 1080;

// ---- Benchmark settings ----
constexpr int   ENTITY_COUNT = 100000;
constexpr float GRID_SPACING = 9.0f;
constexpr float MAX_DRIFT    = 0.35f * GRID_SPACING;
constexpr float DRIFT_STEP   = 0.05f;
constexpr float SPIN_STEP    = 0.05f;

struct Actor
{
    std::unique_ptr<Entity> entity;
    glm::vec3 home;
    glm::vec3 drift{ 0.0f };
    glm::vec3 euler{ 0.0f };
};

static void SetPose(Transform& t, const glm::vec3& position, const glm::vec3& eulerRadians)
{
    t.position = position;
    t.rotation = glm::quat(eulerRadians);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Block Game Animated Benchmark", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    gladLoadGL();
    glViewport(0, 0, width, height);

    {
        MeshLoader meshLoader = MeshLoader();
        MaterialLoader materialLoader = MaterialLoader();

        std::vector<Material> materials =
            materialLoader.Load("../resources/materials/Materials.json");

        Mesh* chickenMesh =
            meshLoader.Load("../resources/models/chicken/chicken.gltf");

        if (chickenMesh == nullptr || materials.size() < 3)
        {
            std::cout << "Failed to load chicken mesh or materials" << std::endl;
            glfwTerminate();
            return -1;
        }

        // ---- Load and bake animation data once ----
        Skeleton skeleton = meshLoader.LoadSkeleton();
        std::vector<AnimationClip> clips =
            meshLoader.LoadAnimationClips(skeleton);

        std::vector<BakedAnimationClip> bakedClips;
        bakedClips.reserve(clips.size());

        for (auto& clip : clips)
        {
            bakedClips.push_back(
                BakeAnimationClip(skeleton, clip, 8)
            );
        }

        BakedAnimationClip* flapClip = nullptr;
        BakedAnimationClip* walkClip = nullptr;

        for (auto& clip : bakedClips)
        {
            if (clip.name == "flap")
                flapClip = &clip;

            if (clip.name == "Walk")
                walkClip = &clip;
        }

        if (flapClip == nullptr || walkClip == nullptr)
        {
            std::cout << "Failed to find flap or Walk animation" << std::endl;
            glfwTerminate();
            return -1;
        }

        Camera camera = Camera(width, height, glm::vec3(0, 0, 0));
        Renderer renderer = Renderer(&camera);

        std::mt19937 rng(12345);

        std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
        std::uniform_int_distribution<int> pickAnimation(0, 1);

        // ---- Build entities on a centred 3D grid ----
        int n = 1;
        while (n * n * n < ENTITY_COUNT)
            ++n;

        const float centre = (n - 1) * 0.5f;

        std::list<Actor> actors;
        //actors.reserve(ENTITY_COUNT);

        for (int i = 0; i < ENTITY_COUNT; ++i)
        {
            const int x = i % n;
            const int y = (i / n) % n;
            const int z = i / (n * n);

            Actor a;

            a.home = glm::vec3(
                (x - centre) * GRID_SPACING,
                (y - centre) * GRID_SPACING,
                (z - centre) * GRID_SPACING
            );

            a.entity = std::make_unique<Entity>();

            a.entity->ID =
                static_cast<decltype(a.entity->ID)>(i + 1);

            a.entity->mesh = chickenMesh;
            a.entity->material = &materials[2];

            SetPose(
                a.entity->transform,
                a.home,
                a.euler
            );

            a.entity->animationPlayer.AddClip(
                "flap",
                flapClip
            );

            a.entity->animationPlayer.AddClip(
                "Walk",
                walkClip
            );

            a.entity->animationPlayer.looping = true;

            if (pickAnimation(rng) == 0)
                a.entity->animationPlayer.Play("flap");
            else
                a.entity->animationPlayer.Play("Walk");

            if (!renderer.AddEntity(*a.entity))
            {
                std::cout << "AddEntity failed for entity "
                          << i << std::endl;
                continue;
            }

            actors.push_back(std::move(a));
        }

        std::cout << "Added "
                  << actors.size()
                  << " / "
                  << ENTITY_COUNT
                  << " animated chickens"
                  << std::endl;

        glEnable(GL_DEPTH_TEST);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // ---- Frame timing ----
        double lastReport = glfwGetTime();
        int frames = 0;

        GLuint q;
        glGenQueries(1, &q);

        glBeginQuery(GL_TIME_ELAPSED, q);
        renderer.Render();
        glEndQuery(GL_TIME_ELAPSED);

        GLuint64 ns = 0;
        glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);

        std::cout << "GPU: "
                  << ns / 1.0e6
                  << " ms"
                  << std::endl;

        double lastFrameTime = glfwGetTime();

        while (!glfwWindowShouldClose(window))
        {
            double currentTime = glfwGetTime();
            double delta = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            camera.Inputs(window);

            for (Actor& a : actors)
            {
                a.drift += glm::vec3(
                    jitter(rng),
                    jitter(rng),
                    jitter(rng)
                ) * DRIFT_STEP;

                a.drift = glm::clamp(
                    a.drift,
                    glm::vec3(-MAX_DRIFT),
                    glm::vec3(MAX_DRIFT)
                );

                a.euler += glm::vec3(
                    jitter(rng),
                    jitter(rng),
                    jitter(rng)
                ) * SPIN_STEP;

                SetPose(
                    a.entity->transform,
                    a.home + a.drift,
                    a.euler
                );

                a.entity->animationPlayer.Update(delta);

                renderer.UpdateEntity(*a.entity);
            }

            renderer.Render();

            glfwSwapBuffers(window);
            glfwPollEvents();

            ++frames;

            const double now = glfwGetTime();

            if (now - lastReport >= 1.0)
            {
                std::cout
                    << frames
                    << " fps ("
                    << 1000.0 * (now - lastReport) / frames
                    << " ms/frame)"
                    << std::endl;

                frames = 0;
                lastReport = now;
            }

            GLenum error = glGetError();

            if (error != GL_NO_ERROR)
                std::cerr << "OpenGL error: "
                          << error
                          << '\n';
        }

        glDeleteQueries(1, &q);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}