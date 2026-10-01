cmake_minimum_required(VERSION 3.10)

project(MyProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(MyProject

    src/Camera.cpp
    src/Allocator.cpp
    src/EntityManager.cpp
    src/glad.c
    src/MaterialLoader.cpp
    src/Mesh.cpp
    src/MeshLoader.cpp
    src/Renderer.cpp
    src/RenderTypes.cpp
    src/Shader.cpp
    src/stb.cpp
    src/Texture.cpp
    
    src/main.cpp
)

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