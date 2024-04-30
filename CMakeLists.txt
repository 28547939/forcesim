cmake_minimum_required( VERSION 2.8.12 )
#set ( CMAKE_BUILD_TYPE Release )

project(forcesim)

set(CMAKE_CXX_FLAGS "-std=c++20 -pthread")

if (CMAKE_BUILD_TYPE EQUAL "Debug" )
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -c -Wall -Wextra")
	add_definitions ( -Wall -Wextra )
elseif(CMAKE_BUILD_TYPE EQUAL "Release")
endif()

set ( forcesim_SRCS
	json_conversion.cpp
	Subscriber/implementation.cpp
	Interface.cpp
	Market.cpp
	Agent/Agent.cpp
)

add_subdirectory(
	${VENDOR}/glog
)

link_libraries ( glog::glog -lboost_program_options -L${INCLUDE_PREFIX}/lib )

add_executable ( forcesim ${forcesim_SRCS} forcesim.cpp )
#add_executable ( test-interface ${forcesim_SRCS} tests/interface.cpp )
add_executable ( test-agent Agent/Agent.cpp json_conversion.cpp Market.cpp Subscriber/implementation.cpp tests/agent.cpp )
add_executable ( test-subscriber Agent/Agent.cpp json_conversion.cpp Market.cpp Subscriber/implementation.cpp tests/subscriber.cpp )
add_executable ( test-ts tests/ts.cpp )
add_executable ( test-timepoint tests/timepoint.cpp )
add_executable ( v2-dist-points Agent/Agent.cpp json_conversion.cpp tests/v2_dist_points.cpp )



include_directories(
	${VENDOR}/Crow/include/
	${VENDOR}/json/include/
	${INCLUDE_PREFIX}/include/
)

