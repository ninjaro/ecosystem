_ecosystem_runtime_asset_files("{{source_root_expression}}/assets" ecosystem_install_assets)
foreach (source IN LISTS ecosystem_install_assets)
    file(RELATIVE_PATH relative "{{source_root_expression}}/assets" "${source}")
    get_filename_component(parent "${relative}" DIRECTORY)
    get_filename_component(name "${source}" NAME)
    get_filename_component(real_source "${source}" REALPATH)
    install(
            FILES "${real_source}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/{{project_id}}/assets/${parent}"
            RENAME "${name}"
    )
endforeach ()
