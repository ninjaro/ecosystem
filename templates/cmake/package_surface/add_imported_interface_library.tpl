if (NOT TARGET {{target_name}})
    add_library({{target_name}} INTERFACE IMPORTED GLOBAL)
endif ()
