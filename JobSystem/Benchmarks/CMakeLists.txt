cmake_minimum_required (VERSION 3.20)

macro(Add_Benchmark_JobSystem TestName CppFiles)
	add_executable(${TestName} ${CppFiles})
	target_link_libraries(${TestName} PRIVATE JobSystem)
	target_include_directories(${TestName} PRIVATE "${PROJECT_SOURCE_DIR}/Src")
	set_property(TARGET ${TestName} PROPERTY CXX_STANDARD 20)
endmacro()

Add_Benchmark_JobSystem(FunctionCallBenchmark "FunctionCallBenchmark.cpp")
Add_Benchmark_JobSystem(ScalingBenchmark "ScalingBenchmark.cpp")
