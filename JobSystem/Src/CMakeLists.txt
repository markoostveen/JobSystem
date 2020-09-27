# CMakeList.txt : CMake project for JobSystem, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
file(GLOB_RECURSE  JobSystemSources
  "JobSystem/*"
)

set(JobSystemHeaderDirectory "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library (JobSystem ${JobSystemSources})
target_include_directories(JobSystem PRIVATE ${JobSystemHeaderDirectory})
set_property(TARGET JobSystem PROPERTY CXX_STANDARD 20)

find_package(Boost REQUIRED)
target_include_directories(JobSystem PUBLIC ${Boost_INCLUDE_DIRS})

#target_compile_definitions(JobSystem PUBLIC DEBUG)
