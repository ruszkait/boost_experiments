cmake_minimum_required(VERSION 3.1)
project(BoostTest CXX)

# Get conan dependencies
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

# To enable unittesting without need to do cmake install
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out CACHE STRING "" )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out CACHE STRING "" )

find_package(Boost REQUIRED)
find_package(Gtest REQUIRED)

add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/SignalTest.cpp
)
 
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        BOOST_THREAD_PROVIDES_FUTURE
        BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        Boost::boost
        Boost::thread
		GTest::gtest
        GTest::gtest_main
)

