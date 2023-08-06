cmake_minimum_required (VERSION 3.20)


find_package(Catch2 CONFIG REQUIRED)
if (NOT ${Catch2_FOUND})
	# Setup and download catch
	Include(FetchContent)
	FetchContent_Declare(
		Catch2
		GIT_REPOSITORY https://github.com/catchorg/Catch2.git
		GIT_TAG        v3.1.0 # or a later release
		)

	FetchContent_MakeAvailable(Catch2)
	target_compile_definitions(Catch2 PUBLIC "CATCH_CONFIG_DISABLE_EXCEPTIONS=1")
endif()


macro(Add_Test_JobSystem TestName CppFiles)
	set(fullTestName Test_${TestName})
	add_executable(${fullTestName} "TestUtils.hpp" ${CppFiles})
	target_link_libraries(${fullTestName} PRIVATE JobSystem)
	target_link_libraries(${fullTestName} PRIVATE Catch2::Catch2WithMain)
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