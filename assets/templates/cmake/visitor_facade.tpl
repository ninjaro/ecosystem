# The visitor name is generated state; the selected target keeps its identity.
# Retire a previous link/copy before a new selection can build at the same path.
foreach(facade_name mvp mvp.exe)
    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ecosystem-${facade_name}.owned")
        file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/${facade_name}"
                    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ecosystem-${facade_name}.owned")
    endif()
endforeach()

get_target_property(ECOSYSTEM_FACADE_TYPE {{target_name}} TYPE)
if(ECOSYSTEM_FACADE_TYPE STREQUAL "EXECUTABLE" AND NOT ANDROID)
    if(WIN32)
        set(ECOSYSTEM_FACADE_NAME mvp.exe)
    else()
        set(ECOSYSTEM_FACADE_NAME mvp)
    endif()
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ecosystem-facade-$<CONFIG>.cmake"
        CONTENT [=[
set(source "$<TARGET_FILE:{{target_name}}>")
set(destination "${FACADE_DIR}/${FACADE_NAME}")
if(source STREQUAL destination)
    return()
endif()
if(WIN32)
    file(CREATE_LINK "${source}" "${destination}" COPY_ON_ERROR)
else()
    file(RELATIVE_PATH relative "${FACADE_DIR}" "${source}")
    file(CREATE_LINK "${relative}" "${destination}" SYMBOLIC)
endif()
file(WRITE "${FACADE_DIR}/CMakeFiles/ecosystem-${FACADE_NAME}.owned" "generated\n")
]=])
    add_custom_target(_ecosystem_visitor_facade_{{project_id}} ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DFACADE_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DFACADE_NAME=${ECOSYSTEM_FACADE_NAME}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ecosystem-facade-$<CONFIG>.cmake"
        DEPENDS {{target_name}}
        VERBATIM)
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${ECOSYSTEM_FACADE_NAME}"
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ecosystem-${ECOSYSTEM_FACADE_NAME}.owned")
endif()
