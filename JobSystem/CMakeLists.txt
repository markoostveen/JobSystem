# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.20)


# Only run standalone configuration when setting it up standalone
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    include(${CMAKE_SOURCE_DIR}/Configure.cmake)
    RunVCPKG()
endif()

project ("JobSystem")

# Include sub-projects.
add_subdirectory ("Src")

option(JOBSYSTEM_TEST "Build unit test projects" ON)


if(${JOBSYSTEM_TEST})
    enable_testing()
    add_subdirectory ("Tests")

    add_subdirectory("Benchmarks")
endif()