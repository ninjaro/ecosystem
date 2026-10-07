foreach (category IN ITEMS {{runtime_categories}})
    install(
            DIRECTORY "{{source_root_expression}}/assets/${category}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/{{project_id}}/assets
            OPTIONAL
    )
endforeach ()
