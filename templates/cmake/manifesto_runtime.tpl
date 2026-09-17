# Runtime data belongs to the tooling install, independently of project assets.
file(RELATIVE_PATH MANIFESTO_INSTALL_TEMPLATE_PATH
     "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_FULL_DATADIR}/manifesto/templates")
set_property(SOURCE "{{source_root_expression}}/core/src/workspace/template_text.cpp"
             APPEND PROPERTY COMPILE_DEFINITIONS
             "MANIFESTO_BUILD_ROOT=\"$<TARGET_FILE_DIR:{{runtime_target}}>\""
             "MANIFESTO_SOURCE_TEMPLATE_ROOT=\"{{source_root_expression}}/templates\""
             "MANIFESTO_INSTALL_TEMPLATE_PATH=\"${MANIFESTO_INSTALL_TEMPLATE_PATH}\"")
install(DIRECTORY "{{source_root_expression}}/templates/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/manifesto/templates")
