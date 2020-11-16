cmake_minimum_required (VERSION 3.8)

macro(Add_Test_JobSystem TestName CppFiles)
	set(fullTestName Test_${TestName})
	add_executable(${fullTestName} "TestUtils.hpp" "TestingFramework/catch.hpp" ${CppFiles})
	target_link_libraries(${fullTestName} PRIVATE JobSystem)
	target_include_directories(${fullTestName} PRIVATE "${PROJECT_SOURCE_DIR}/Src")
	set_property(TARGET ${fullTestName} PROPERTY CXX_STANDARD 20)
	add_test(NAME JobSystem_${TestName} COMMAND "$<TARGET_FILE:${fullTestName}>")
endmacro()

#Normal job types
Add_Test_JobSystem(Initialization Initialization.cpp)
Add_Test_JobSystem(Singleton Singleton.cpp)
Add_Test_JobSystem(SingleJobs SingleJob.cpp)
Add_Test_JobSystem(MultiJobs MultiJobs.cpp)
Add_Test_JobSystem(Parallel ParallelJobs.cpp)
Add_Test_JobSystem(LargeJob LargeJob.cpp)