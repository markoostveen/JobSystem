# CMakeList.txt : CMake project for JobSystem, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
add_executable (JobSystem "Job.cpp" "JobSystem.h" "main.cpp" "Job.h" "JobSystem.cpp" "JobSystemThread.cpp" )
set_property(TARGET JobSystem PROPERTY CXX_STANDARD 20)

# TODO: Add tests and install targets if needed.
