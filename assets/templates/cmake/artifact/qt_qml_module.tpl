if (COMMAND qt_policy)
    if (Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
        qt_policy(SET QTP0001 NEW)
    endif ()
    if (Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
        qt_policy(SET QTP0004 NEW)
    endif ()
endif ()
{{resource_alias_block}}
qt_add_qml_module({{target_name}}
    URI {{qml_uri}}
    VERSION {{qml_version}}
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/{{target_name}}/{{qml_uri_path}}"
    QML_FILES
{{qml_files_block}})
