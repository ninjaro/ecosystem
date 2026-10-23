{{generated_notice}}

cmake_minimum_required(VERSION 3.20)

project({{project_id}} VERSION {{project_version}} LANGUAGES C CXX)

{{developer_options_block}}set(CMAKE_CXX_STANDARD {{cpp_standard}})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_STANDARD EQUAL 26 AND CMAKE_VERSION VERSION_LESS 3.25)
    message(FATAL_ERROR "Ecosystem experimental C++26 requires CMake 3.25 or newer.")
endif ()
if (NOT "cxx_std_{{cpp_standard}}" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR "Ecosystem requires C++{{cpp_standard}}; ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not advertise it. Select a supported compiler or change manifest.cpp_standard after checking project requirements.")
endif ()
if (CMAKE_CXX_STANDARD EQUAL 26)
    message(WARNING "Ecosystem: C++26 is experimental; compiler and standard-library feature support must be verified by the project.")
endif ()
set(ECOSYSTEM_RESOLVED_CXX_STANDARD ${CMAKE_CXX_STANDARD} CACHE INTERNAL "Resolved C++ standard" FORCE)
set(ECOSYSTEM_RESOLVED_CXX_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" CACHE INTERNAL "Resolved C++ compiler" FORCE)

include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

{{package_surface}}{{apply_defaults_block}}{{assets_block}}
