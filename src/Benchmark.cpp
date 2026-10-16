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

const unsigned int width  = 1920;
const unsigned int height = 1080;

// ---- Benchmark settings ----
constexpr int   ENTITY_COUNT = 10000;
constexpr float GRID_SPACING = 9.0f;
constexpr float MAX_DRIFT    = 0.35f * GRID_SPACING; // max wander from home cell, so neighbours never overlap
constexpr float DRIFT_STEP   = 0.05f;                // max random-walk step per axis per frame
constexpr float SPIN_STEP    = 0.05f;                // max random rotation change per axis per frame (radians)

struct Actor
{
    std::unique_ptr<Entity> entity; // heap-allocated: EntityManager keys by Entity address, so it must never move
    glm::vec3 home;                 // centre of this entity's grid cell
    glm::vec3 drift{ 0.0f };        // current offset from home
    glm::vec3 euler{ 0.0f };        // current rotation, radians
};

// The only place that touches Transform's fields - adapt here if your Transform differs.
// Assumes position is a glm::vec3 and rotation is a glm::quat.
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

    // Scene lives in its own scope so the Renderer (and its GL objects) is destroyed
    // while the context still exists - before glfwDestroyWindow/glfwTerminate below.
    {
        MeshLoader meshLoader = MeshLoader();
        MaterialLoader materialLoader = MaterialLoader();

        std::vector<Material> materials = materialLoader.Load("../resources/materials/Materials.json");

        // Load each mesh once; every entity using it shares the same Mesh (and so the same GPU copy).
        Mesh* meshes[2] = {
            meshLoader.Load("../resources/models/methaneMonster/model.gltf"),
            meshLoader.Load("../resources/models/maxSternum/model.gltf"),
        };
        if (meshes[0] == nullptr || meshes[1] == nullptr || materials.size() < 2)
        {
            std::cout << "Failed to load meshes or materials" << std::endl;
            glfwTerminate();
            return -1;
        }

        // Pulled back to see the whole grid (~90 units across). If the back of the grid
        // is clipped, raise the camera's far plane.
        Camera camera = Camera(width, height, glm::vec3(0, 0, 0));
        Renderer renderer = Renderer(&camera);

        std::mt19937 rng(12345); // fixed seed so benchmark runs are comparable
        std::uniform_int_distribution<int>    pickMesh(0, 1);
        std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);

        // ---- Build the entities on a centred 3D grid ----
        int n = 1;
        while (n * n * n < ENTITY_COUNT) ++n; // 10 for 1000
        const float centre = (n - 1) * 0.5f;

        std::vector<Actor> actors;
        actors.reserve(ENTITY_COUNT);

        for (int i = 0; i < ENTITY_COUNT; ++i)
        {
            const int x = i % n;
            const int y = (i / n) % n;
            const int z = i / (n * n);

            Actor a;
            a.home = glm::vec3((x - centre) * GRID_SPACING,
                               (y - centre) * GRID_SPACING,
                               (z - centre) * GRID_SPACING);

            const int meshIndex = pickMesh(rng);               // 0 = methaneMonster, 1 = maxSternum

			a.entity = std::make_unique<Entity>();
			a.entity->ID       = static_cast<decltype(a.entity->ID)>(i + 1);
			a.entity->mesh     = meshes[meshIndex];
			a.entity->material = &materials.at(meshIndex);     // methaneMonster -> material 0, maxSternum -> material 1
			SetPose(a.entity->transform, a.home, a.euler);

            if (!renderer.AddEntity(*a.entity))
            {
                std::cout << "AddEntity failed for entity " << i << std::endl;
                continue;
            }
            actors.push_back(std::move(a));
        }
        std::cout << "Added " << actors.size() << " / " << ENTITY_COUNT << " entities" << std::endl;

        glEnable(GL_DEPTH_TEST);

        // ---- Frame timing ----
        double lastReport = glfwGetTime();
        int    frames     = 0;
		
		GLuint q; glGenQueries(1, &q);   // once, before the loop

		glBeginQuery(GL_TIME_ELAPSED, q);
		renderer.Render();
		glEndQuery(GL_TIME_ELAPSED);

		GLuint64 ns = 0;
		glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns); // stalls the CPU, fine for diagnosis
		std::cout << "GPU: " << ns / 1.0e6 << " ms\n";

        while (!glfwWindowShouldClose(window))
        {
            glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            camera.Inputs(window);

            // Random walk: small random step each frame, clamped so entities stay near their own cell.
            for (Actor& a : actors)
            {
                a.drift += glm::vec3(jitter(rng), jitter(rng), jitter(rng)) * DRIFT_STEP;
                a.drift  = glm::clamp(a.drift, glm::vec3(-MAX_DRIFT), glm::vec3(MAX_DRIFT));
                a.euler += glm::vec3(jitter(rng), jitter(rng), jitter(rng)) * SPIN_STEP;

                SetPose(a.entity->transform, a.home + a.drift, a.euler);
                renderer.UpdateEntity(*a.entity);
            }

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
        }
    } // renderer destroyed here, context still alive

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}