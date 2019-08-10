cmake_minimum_required(VERSION 3.9)

project(cobblectl)

include(ExternalProject)

set(BUILD_SHARED_LIBS OFF)
option(BUILD_STATIC_STONECTL OFF)

add_subdirectory(deps/wsrpc EXCLUDE_FROM_ALL)
add_subdirectory(deps/CLI11 EXCLUDE_FROM_ALL)

add_executable(cobblectl src/main.cpp)
if(${BUILD_STATIC_STONECTL})
  target_link_libraries(cobblectl -static)
endif()
target_link_libraries(cobblectl rpcws CLI11 stdc++fs pthread)
set_property(TARGET cobblectl PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
set_property(TARGET cobblectl PROPERTY CXX_STANDARD 17)

install(TARGETS cobblectl
        RUNTIME DESTINATION bin)