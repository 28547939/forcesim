cmake_minimum_required( VERSION 3.29 )
#set ( CMAKE_BUILD_TYPE Release )

project(forcesim)

set(CMAKE_CXX_FLAGS "-std=c++20 -pthread")

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE EQUAL "Debug" )
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -c -Wall -Wextra")
	add_definitions ( -Wall -Wextra )
elseif(CMAKE_BUILD_TYPE EQUAL "Release")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -c -Wall -Wextra")
endif()

set ( core_src
	json_conversion.cpp
	Subscriber/implementation.cpp
	Interface.cpp
	Market.cpp
	Agent/Agent.cpp
)


add_subdirectory(
	${VENDOR}/glog
)

find_package(Boost REQUIRED COMPONENTS program_options)

#set(main_lib glog::glog Boost::program_options)

add_library(forcesim_core STATIC ${core_src})
target_link_libraries(forcesim_core PRIVATE Boost::program_options)
target_link_libraries(forcesim_core PUBLIC glog::glog)
target_include_directories(forcesim_core PUBLIC
	${VENDOR}/Crow/include/
	${VENDOR}/json/include/
	${INCLUDE_PREFIX}/include/
)




add_executable ( forcesim forcesim.cpp )
target_link_libraries(forcesim PRIVATE forcesim_core )
#target_link_libraries(forcesim PUBLIC glog::glog )
#add_executable ( test-interface ${forcesim_SRCS} tests/interface.cpp )

add_executable ( test-agent tests/agent.cpp )
target_link_libraries(test-agent PRIVATE forcesim_core )

add_executable ( test-subscriber tests/subscriber.cpp )
target_link_libraries(test-subscriber PRIVATE forcesim_core)

add_executable ( 
	test-info tests/info.cpp 
)
target_link_libraries(test-info PRIVATE forcesim_core)

add_executable ( v2-dist-points tests/v2_dist_points.cpp )
target_link_libraries(v2-dist-points PRIVATE forcesim_core)

add_executable ( test-ts tests/ts.cpp )
target_link_libraries(test-ts PRIVATE glog::glog)

add_executable ( test-timepoint tests/timepoint.cpp )
target_link_libraries(test-timepoint PRIVATE glog::glog )



