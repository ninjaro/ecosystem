    # Package discovery happens in a separate compiler-enabled configure. Qt and
    # other imported targets must not leak into this LANGUAGES NONE outer graph.
    file(WRITE "${_ecosystem_state}/probe/CMakeLists.txt" [==[
cmake_minimum_required(VERSION 3.20)
project(facade_sdk_probe LANGUAGES C CXX)
include("${SELECTION}")
find_package(${PACKAGE} CONFIG QUIET)
if(${PACKAGE}_FOUND)
    foreach(target IN LISTS TARGETS)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "Installed SDK ${PACKAGE} does not export ${target}; install the required SDK or prepare it with Marx")
        endif()
    endforeach()
    file(WRITE "${RESULT}" "${${PACKAGE}_DIR}")
else()
    file(WRITE "${RESULT}" "")
endif()
]==])
    set(_ecosystem_installed_cache "")
    function(_ecosystem_source package repository revision)
        string(SHA256 identity "${package}\n${repository}\n${revision}")
        _ecosystem_bracket(package_value "${package}")
        _ecosystem_bracket(target_values "${ARGN}")
        set(selection "${_ecosystem_state}/probe/${identity}-selection.cmake")
        file(WRITE "${selection}" "set(PACKAGE ${package_value})\nset(TARGETS ${target_values})\n")
        set(result "${_ecosystem_state}/probe/${identity}-result.txt")
        # Reconfigure the probe on every outer configure; missing or changed
        # installed SDK evidence must never reuse an earlier selection.
        file(REMOVE "${result}")
        set(generator_args -G "${CMAKE_GENERATOR}")
        if(CMAKE_GENERATOR_PLATFORM)
            list(APPEND generator_args -A "${CMAKE_GENERATOR_PLATFORM}")
        endif()
        if(CMAKE_GENERATOR_TOOLSET)
            list(APPEND generator_args -T "${CMAKE_GENERATOR_TOOLSET}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" ${generator_args}
            -S "${_ecosystem_state}/probe" -B "${_ecosystem_state}/probe/${identity}"
            -C "${_ecosystem_state}/cache.cmake" "-DSELECTION:FILEPATH=${selection}"
            "-DRESULT:FILEPATH=${result}" "-D${package}_DIR:PATH=${${package}_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
        if(NOT status STREQUAL "0" OR NOT EXISTS "${result}")
            message(FATAL_ERROR "Installed provider probe failed for ${package}: ${status}\n${output}\n${errors}")
        endif()
        file(READ "${result}" directory)
        if(NOT directory STREQUAL "")
            _ecosystem_bracket(directory "${directory}")
            string(APPEND _ecosystem_installed_cache
                "set(${package}_DIR ${directory} CACHE PATH \"Selected installed SDK\" FORCE)\n")
            set(_ecosystem_installed_cache "${_ecosystem_installed_cache}" PARENT_SCOPE)
            message(STATUS "Facade provider ${package}: installed SDK")
            return()
        endif()
        if(identity IN_LIST ECOSYSTEM_FACADE_ANCESTRY)
            message(FATAL_ERROR "Cyclic facade provider selection: ${package}, ${repository} @ ${revision}")
        endif()
        set(source "${_ecosystem_state}/sources/${identity}")
        set(acquire "_ecosystem_source_${package}_${identity}")
        ExternalProject_Add(${acquire}
            PREFIX "${_ecosystem_state}/acquire/${identity}"
            SOURCE_DIR "${source}"
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${revision}"
            # A new authored selection gets a new tree. Rebuilds do not fetch.
            UPDATE_COMMAND ""
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND ""
            EXCLUDE_FROM_ALL TRUE)
        list(APPEND _ecosystem_providers "${identity}")
        set(_ecosystem_providers "${_ecosystem_providers}" PARENT_SCOPE)
        set(_ecosystem_package_${identity} "${package}" PARENT_SCOPE)
        set(_ecosystem_description_${identity} "Provider ${package}: ${repository} @ ${revision}" PARENT_SCOPE)
    endfunction()
{{provider_declarations}}
    # Validate the installed contract, not a provider's source/build layout.
    file(WRITE "${_ecosystem_state}/packages.cmake" [==[
include("${INPUTS}")
function(bracket result value)
    set(equal "=")
    string(FIND "${value}" "]${equal}]" found)
    while(NOT found EQUAL -1)
        string(APPEND equal "=")
        string(FIND "${value}" "]${equal}]" found)
    endwhile()
    set(${result} "[${equal}[${value}]${equal}]" PARENT_SCOPE)
endfunction()
set(contents "${INSTALLED_CACHE}")
foreach(package IN LISTS PACKAGES)
    file(GLOB_RECURSE configs "${PREFIX}/${package}Config.cmake")
    list(LENGTH configs count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Provider ${package} must install exactly one ${package}Config.cmake under ${PREFIX}; found ${count}. Plain CMake acquires only facade exports; non-facade exports require an installed SDK or Marx preparation.")
    endif()
    list(GET configs 0 config)
    get_filename_component(directory "${config}" DIRECTORY)
    bracket(value "${directory}")
    string(APPEND contents "set(${package}_DIR ${value} CACHE PATH \"Selected facade provider\" FORCE)\n")
endforeach()
bracket(value "${PREFIX};${PREVIOUS_PREFIX}")
string(APPEND contents "set(CMAKE_PREFIX_PATH ${value} CACHE STRING \"Facade provider closure\" FORCE)\n")
file(WRITE "${OUTPUT}.in" "${contents}")
configure_file("${OUTPUT}.in" "${OUTPUT}" COPYONLY)
]==])
