# CMakeList.txt : CMake project for JobSystem, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

option(JOBSYSTEM_KEEP_ALIVE "Allow JobSystem Workers to exit when they are not needed" OFF)
option(JOBSYSTEM_ANALYTICS_ENABLED "Allow JobSystem Workers to collect analytics" OFF)


if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "CLANG" )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread")
  endif()

# Add source to this project's executable.
file(GLOB_RECURSE  JobSystemSources
  "JobSystem/*.cpp"
)

set(JobSystemHeaderDirectory "${CMAKE_CURRENT_SOURCE_DIR}/")

find_package(Threads REQUIRED)

add_library (JobSystem STATIC ${JobSystemSources})
if(${JOBSYSTEM_KEEP_ALIVE})
  target_compile_definitions(JobSystem PUBLIC JBSYSTEM_KEEP_ALIVE)
endif()

if(${JOBSYSTEM_ANALYTICS_ENABLED})
  target_compile_definitions(JobSystem PUBLIC JobSystem_Analytics_Enabled)
endif()

target_include_directories(JobSystem PUBLIC ${JobSystemHeaderDirectory})
set_property(TARGET JobSystem PROPERTY CXX_STANDARD 23)

target_link_libraries(JobSystem PRIVATE Threads::Threads)

find_package(Boost REQUIRED)
target_include_directories(JobSystem PUBLIC ${Boost_INCLUDE_DIRS})

#target_compile_definitions(JobSystem PUBLIC DEBUG)
