{{generated_notice}}

cmake_minimum_required(VERSION 3.20)

project({{project_id}} VERSION {{project_version}} LANGUAGES C CXX)

{{developer_options_block}}set(CMAKE_CXX_STANDARD {{cpp_standard}})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

{{package_surface}}{{apply_defaults_block}}{{assets_block}}
