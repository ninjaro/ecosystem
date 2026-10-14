# Runtime data belongs to the tooling install, independently of project assets.
file(RELATIVE_PATH ECOSYSTEM_INSTALL_TEMPLATE_PATH
     "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_FULL_DATADIR}/ecosystem/templates")
set_property(SOURCE "{{source_root_expression}}/core/src/workspace/template_text.cpp"
             APPEND PROPERTY COMPILE_DEFINITIONS
             "ECOSYSTEM_BUILD_ROOT=\"$<TARGET_FILE_DIR:{{runtime_target}}>\""
             "ECOSYSTEM_SOURCE_TEMPLATE_ROOT=\"{{source_root_expression}}/assets/templates\""
             "ECOSYSTEM_INSTALL_TEMPLATE_PATH=\"${ECOSYSTEM_INSTALL_TEMPLATE_PATH}\"")
if (COMMAND _ecosystem_runtime_asset_files AND IS_DIRECTORY "{{source_root_expression}}/assets/templates")
    _ecosystem_runtime_asset_files("{{source_root_expression}}/assets" ecosystem_templates templates)
    foreach (source IN LISTS ecosystem_templates)
        file(RELATIVE_PATH relative "{{source_root_expression}}/assets/templates" "${source}")
        get_filename_component(parent "${relative}" DIRECTORY)
        get_filename_component(name "${source}" NAME)
        get_filename_component(real_source "${source}" REALPATH)
        install(FILES "${real_source}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/ecosystem/templates/${parent}"
                RENAME "${name}")
    endforeach ()
else ()
    install(CODE "message(FATAL_ERROR \"Missing tooling template bundle: {{source_root_expression}}/assets/templates\")")
endif ()
