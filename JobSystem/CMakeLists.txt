# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.20)

project ("JobSystem")

# Include sub-projects.
add_subdirectory ("Src")

option(JOBSYSTEM_TEST "Build unit test projects" OFF)

if(${JOBSYSTEM_TEST})
    enable_testing()
    include(Dart)
    add_subdirectory ("Tests")

    add_subdirectory("Benchmarks")
endif()