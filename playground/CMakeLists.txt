cmake_minimum_required(VERSION 3.25.0)

function(def_playground name source)
    add_executable(${name} ${source})
    target_link_libraries(${name}
        PRIVATE STDEXEC::stdexec
                stdexec_executable_flags)
endfunction()

def_playground(playground                     main.cpp)
def_playground(playground.coroutine           coroutine.cpp)
def_playground(playground.cancellation        cancellation.cpp)
def_playground(playground.custom_sender       custom_sender.cpp)
def_playground(playground.when_any            when_any.cpp)
def_playground(playground.repeat              repeat.cpp)
def_playground(playground.advanced_patterns   advanced_patterns.cpp)
def_playground(playground.handler_chaining    handler_chaining.cpp)
