cmake_minimum_required(VERSION 3.10)

project(MyProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(MyProject

    src/Camera.cpp
    src/Allocator.cpp
    src/RenderManager.cpp
    src/glad.c
    src/MaterialLoader.cpp
    src/Mesh.cpp
    src/MeshLoader.cpp
    src/Renderer.cpp
    src/RenderTypes.cpp
    src/Shader.cpp
    src/stb.cpp
    src/Texture.cpp
    src/Animation.cpp
    src/AnimationPlayer.cpp
    src/Chunk.cpp
    src/ChunkMesher.cpp
    src/VoxelTypes.cpp
    src/WorldGenerator.cpp
    src/ResourceController.cpp
    src/WorldManager.cpp
    src/BlockLoader.cpp

    src/editor/EntityEditor.cpp
    src/editor/ResourceEditor.cpp
    src/Collision.cpp

    #src/main.cpp
    #src/BenchmarkAnimated.cpp
    #src/editor/editor.cpp
    # src/tests/singleChunkTest.cpp
    src/tests/multiChunkTest.cpp

    
)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

   target_sources(MyProject PRIVATE
       ${IMGUI_DIR}/imgui.cpp
       ${IMGUI_DIR}/imgui_draw.cpp
       ${IMGUI_DIR}/imgui_tables.cpp
       ${IMGUI_DIR}/imgui_widgets.cpp
       ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
       ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
   )
   target_include_directories(MyProject PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

file(GLOB_RECURSE INCLUDE_PATHS
    LIST_DIRECTORIES true
    "${CMAKE_SOURCE_DIR}/include/*"
)

set(INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/include"
)

foreach(PATH ${INCLUDE_PATHS})
    if(IS_DIRECTORY "${PATH}")
        list(APPEND INCLUDE_DIRS "${PATH}")
    endif()
endforeach()

target_include_directories(MyProject PRIVATE
    ${INCLUDE_DIRS}
)

target_link_libraries(MyProject PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/libglfw3.a
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(MyProject PRIVATE -static-libgcc -static-libstdc++)

    # MinGW only: also fold in libwinpthread so no DLLs are needed at all
    if(MINGW)
        target_link_options(MyProject PRIVATE -static)
    endif()
endif()