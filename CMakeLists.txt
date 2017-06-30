cmake_minimum_required (VERSION 3.1)

set(CMAKE_CXX_FLAGS "-std=c++14")
set(CMAKE_CXX_STANDARD 14)

project (tcp_lb)

set(ELLE_INSTALL_DIR "/usr/local/elle")

include_directories(${ELLE_INSTALL_DIR}/include)

link_directories(${ELLE_INSTALL_DIR}/lib)

add_executable(tcp_lb tcp_lb.cpp)

target_link_libraries(tcp_lb libelle_core.a libelle_reactor.a libboost_system-1_60.dylib)
