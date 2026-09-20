cmake_minimum_required(VERSION 3.20)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "linux+elf")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
find_program(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND NAMES objdump REQUIRED)
set(executables
{{executables}})
set(libraries
{{libraries}})
set(directories
{{directories}})
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${executables}
    LIBRARIES ${libraries}
    DIRECTORIES ${directories}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts)
if(unresolved)
    message(FATAL_ERROR "Unresolved package runtime libraries: ${unresolved}")
endif()
if(conflicts_FILENAMES)
    message(FATAL_ERROR "Conflicting package runtime libraries: ${conflicts_FILENAMES}")
endif()
file(WRITE {{resolved_path}} "")
foreach(dependency IN LISTS resolved)
    file(APPEND {{resolved_path}} "${dependency}\n")
endforeach()
