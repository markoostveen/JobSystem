# CMakeList.txt : CMake project for JobSystem, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
file(GLOB_RECURSE  JobSystemSources
  "JobSystem/*.cpp"
)

set(JobSystemHeaderDirectory "${CMAKE_CURRENT_SOURCE_DIR}/")

set(CxxStandard 20)

add_library (JobSystem ${JobSystemSources})
target_include_directories(JobSystem PRIVATE ${JobSystemHeaderDirectory})
set_property(TARGET JobSystem PROPERTY CXX_STANDARD ${CxxStandard})
#target_compile_definitions(JobSystem PUBLIC DEBUG)

enable_testing()

macro(Add_Test_JobSystem TestName CppFiles)
	set(fullTestName Test_${TestName})
	add_executable(${fullTestName} ${CppFiles})
	target_link_libraries(${fullTestName} PRIVATE JobSystem)
	target_include_directories(${fullTestName} PRIVATE ${JobSystemHeaderDirectory})
	set_property(TARGET ${fullTestName} PROPERTY CXX_STANDARD ${CxxStandard})
	add_test(NAME JobSystem_${TestName} COMMAND ${fullTestName})
endmacro()

Add_Test_JobSystem(Main main.cpp)
Add_Test_JobSystem(Initialization Tests/Initialization.cpp)
Add_Test_JobSystem(SingleJob Tests/SingleJob.cpp)
Add_Test_JobSystem(MultiJobs_Independend Tests/MultiJobs_Independend.cpp)
