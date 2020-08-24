# CMakeList.txt : CMake project for JobSystem, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
add_executable (JobSystem "Core/InternalJob.cpp" "Core/JobSystem.h" "main.cpp" "Core/InternalJob.h" "Core/JobSystem.cpp" "Core/JobSystemThread.cpp"  )
set_property(TARGET JobSystem PROPERTY CXX_STANDARD 20)
target_compile_definitions(JobSystem PUBLIC DEBUG)

# TODO: Add tests and install targets if needed.
