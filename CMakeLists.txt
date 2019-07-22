cmake_minimum_required(VERSION 3.9)

project(cobblectl)

include(ExternalProject)

set(BUILD_SHARED_LIBS OFF)
option(BUILD_STATIC_STONECTL OFF)

add_subdirectory(deps/wsrpc EXCLUDE_FROM_ALL)
add_subdirectory(deps/CLI11 EXCLUDE_FROM_ALL)

set(ROOTFS ${CMAKE_BINARY_DIR}/rootfs)
file(MAKE_DIRECTORY ${ROOTFS}/include)

ExternalProject_Add(editline_ep
  PREFIX deps/editline
  INSTALL_DIR ${ROOTFS}
  URL https://github.com/troglobit/editline/releases/download/1.16.1/editline-1.16.1.tar.xz
  URL_HASH MD5=df6d7deac03da1b627f9252db984820e
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND ./configure -prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)

add_library(editline INTERFACE IMPORTED)
add_dependencies(editline editline_ep)
target_link_libraries(editline INTERFACE
  ${ROOTFS}/lib/libeditline.a
)
target_include_directories(editline INTERFACE ${ROOTFS}/include)

add_executable(cobblectl src/main.cpp)
if(${BUILD_STATIC_STONECTL})
  target_link_libraries(cobblectl -static)
endif()
target_link_libraries(cobblectl rpcws CLI11 editline stdc++fs)
set_property(TARGET cobblectl PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
set_property(TARGET cobblectl PROPERTY CXX_STANDARD 17)

install(TARGETS cobblectl
        RUNTIME DESTINATION bin)