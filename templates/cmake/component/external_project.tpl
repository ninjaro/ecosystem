find_package({{package}} CONFIG REQUIRED)
if (NOT TARGET {{provider_target}})
    message(FATAL_ERROR "Package {{package}} does not export {{provider_target}}")
endif ()
{{imported_targets_block}}
