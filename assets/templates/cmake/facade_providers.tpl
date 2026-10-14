    function(_ecosystem_source package repository revision)
        string(SHA256 identity "${package}\n${repository}\n${revision}")
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
set(contents "")
foreach(package IN LISTS PACKAGES)
    file(GLOB_RECURSE configs "${PREFIX}/${package}Config.cmake")
    list(LENGTH configs count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Provider ${package} must install exactly one ${package}Config.cmake under ${PREFIX}; found ${count}")
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
