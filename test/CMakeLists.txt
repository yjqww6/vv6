cmake_minimum_required(VERSION 3.15)

find_package(Boost REQUIRED COMPONENTS)

add_executable(test-vv6 main.cpp)
target_link_libraries(test-vv6 PUBLIC vv6 Boost::boost)
add_test(NAME test-vv6 COMMAND test-vv6)
