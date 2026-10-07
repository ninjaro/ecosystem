    if (COMMAND qt_add_resources)
        set(ecosystem_asset_files)
        foreach (category IN ITEMS {{runtime_categories}})
            file(GLOB_RECURSE ecosystem_category_files
                    CONFIGURE_DEPENDS
                    "{{source_root_expression}}/assets/${category}/*"
            )
            list(APPEND ecosystem_asset_files ${ecosystem_category_files})
        endforeach ()
        if (ecosystem_asset_files)
            qt_add_resources(${target_name} "{{project_id}}_assets"
                    PREFIX "/{{project_id}}"
                    BASE "{{source_root_expression}}/assets"
                    FILES ${ecosystem_asset_files}
            )
        endif ()
    endif ()
