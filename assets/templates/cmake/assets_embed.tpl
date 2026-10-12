    if (COMMAND qt_add_resources)
        _ecosystem_runtime_asset_files("{{source_root_expression}}/assets" ecosystem_asset_files)
        if (ecosystem_asset_files)
            qt_add_resources(${target_name} "{{project_id}}_assets"
                    PREFIX "/{{project_id}}"
                    BASE "{{source_root_expression}}/assets"
                    FILES ${ecosystem_asset_files}
            )
        endif ()
    endif ()
