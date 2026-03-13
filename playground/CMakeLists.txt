cmake_minimum_required(VERSION 3.25.0)

add_executable(playground main.cpp)
target_link_libraries(playground
    PRIVATE STDEXEC::stdexec
            stdexec_executable_flags)
