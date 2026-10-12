    function(_manifesto_source package repository revision)
        string(SHA256 identity "${package}\n${repository}\n${revision}")
        if(identity IN_LIST MANIFESTO_FACADE_ANCESTRY)
            message(FATAL_ERROR "Cyclic facade provider selection: ${package}, ${repository} @ ${revision}")
        endif()
        set(source "${_manifesto_state}/sources/${identity}")
        set(acquire "_manifesto_source_${package}_${identity}")
        ExternalProject_Add(${acquire}
            PREFIX "${_manifesto_state}/acquire/${identity}"
            SOURCE_DIR "${source}"
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${revision}"
            # A new authored selection gets a new tree. Rebuilds do not fetch.
            UPDATE_COMMAND ""
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND ""
            EXCLUDE_FROM_ALL TRUE)
        list(APPEND _manifesto_providers "${identity}")
        set(_manifesto_providers "${_manifesto_providers}" PARENT_SCOPE)
        set(_manifesto_package_${identity} "${package}" PARENT_SCOPE)
        set(_manifesto_description_${identity} "Provider ${package}: ${repository} @ ${revision}" PARENT_SCOPE)
    endfunction()
{{provider_declarations}}
    # Validate the installed contract, not a provider's source/build layout.
    file(WRITE "${_manifesto_state}/packages.cmake" [==[
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
