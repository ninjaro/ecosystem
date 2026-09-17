    if (COMMAND qt_add_resources)
        file(GLOB_RECURSE ecosystem_asset_files
                CONFIGURE_DEPENDS
                "{{source_root_expression}}/assets/*"
        )
        if (ecosystem_asset_files)
            qt_add_resources(${target_name} "{{project_id}}_assets"
                    PREFIX "/{{project_id}}"
                    BASE "{{source_root_expression}}/assets"
                    FILES ${ecosystem_asset_files}
            )
        endif ()
    endif ()
