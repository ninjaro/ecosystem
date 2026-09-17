if (COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif ()
{{resource_alias_block}}
qt_add_qml_module({{target_name}}
    URI {{qml_uri}}
    VERSION {{qml_version}}
    QML_FILES
{{qml_files_block}})
