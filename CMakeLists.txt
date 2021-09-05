cmake_minimum_required(VERSION 3.1)
project(boost_signal_experiments CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(boost COMPONENTS boost thread REQUIRED)
find_package(boost COMPONENTS boost signal2 REQUIRED)
find_package(Gtest)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/SubscriptionTest.cpp
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

