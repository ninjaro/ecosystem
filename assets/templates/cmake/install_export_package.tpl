include(CMakePackageConfigHelpers)
set(_{{project_id}}_package_dir "${CMAKE_CURRENT_BINARY_DIR}/{{project_id}}-package")
file(MAKE_DIRECTORY "${_{{project_id}}_package_dir}")
file(WRITE "${_{{project_id}}_package_dir}/{{project_id}}Config.cmake.in" [==[
if (TARGET {{first_target}})
    return()
endif ()
{{package_surface}}
include("${CMAKE_CURRENT_LIST_DIR}/{{project_id}}Targets.cmake")
]==])
configure_file("${_{{project_id}}_package_dir}/{{project_id}}Config.cmake.in"
               "${_{{project_id}}_package_dir}/{{project_id}}Config.cmake" @ONLY)
write_basic_package_version_file("${_{{project_id}}_package_dir}/{{project_id}}ConfigVersion.cmake"
                                VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT {{project_id}}Targets NAMESPACE {{project_id}}::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/{{project_id}}")
install(FILES "${_{{project_id}}_package_dir}/{{project_id}}Config.cmake"
              "${_{{project_id}}_package_dir}/{{project_id}}ConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/{{project_id}}")
