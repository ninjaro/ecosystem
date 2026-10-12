# Runtime data belongs to the tooling install, independently of project assets.
file(RELATIVE_PATH MANIFESTO_INSTALL_TEMPLATE_PATH
     "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_FULL_DATADIR}/manifesto/templates")
set_property(SOURCE "{{source_root_expression}}/core/src/workspace/template_text.cpp"
             APPEND PROPERTY COMPILE_DEFINITIONS
             "MANIFESTO_BUILD_ROOT=\"$<TARGET_FILE_DIR:{{runtime_target}}>\""
             "MANIFESTO_SOURCE_TEMPLATE_ROOT=\"{{source_root_expression}}/assets/templates\""
             "MANIFESTO_INSTALL_TEMPLATE_PATH=\"${MANIFESTO_INSTALL_TEMPLATE_PATH}\"")
if (COMMAND _ecosystem_runtime_asset_files AND IS_DIRECTORY "{{source_root_expression}}/assets/templates")
    _ecosystem_runtime_asset_files("{{source_root_expression}}/assets" manifesto_templates templates)
    foreach (source IN LISTS manifesto_templates)
        file(RELATIVE_PATH relative "{{source_root_expression}}/assets/templates" "${source}")
        get_filename_component(parent "${relative}" DIRECTORY)
        get_filename_component(name "${source}" NAME)
        get_filename_component(real_source "${source}" REALPATH)
        install(FILES "${real_source}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/manifesto/templates/${parent}"
                RENAME "${name}")
    endforeach ()
else ()
    install(CODE "message(FATAL_ERROR \"Missing tooling template bundle: {{source_root_expression}}/assets/templates\")")
endif ()
