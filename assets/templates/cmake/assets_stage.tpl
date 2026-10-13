# Script mode has its own policy scope; it does not inherit the project policy.
cmake_minimum_required(VERSION 3.20)
{{assets_inventory_functions}}
if (NOT IS_DIRECTORY "${asset_source}" OR IS_SYMLINK "${asset_source}")
    message(FATAL_ERROR "Invalid runtime asset source: ${asset_source}")
endif ()
if (IS_SYMLINK "${asset_destination}" OR
        (EXISTS "${asset_destination}" AND NOT IS_DIRECTORY "${asset_destination}"))
    message(FATAL_ERROR "Invalid runtime asset destination: ${asset_destination}")
endif ()
# Executables beside one another share this output. configure_file replaces
# destination files, so parallel post-build scripts must not copy or prune the
# same tree concurrently. The process lock is destination-local and
# lives beside the payload, not inside it; unrelated destinations stay parallel.
get_filename_component(asset_destination "${asset_destination}" ABSOLUTE)
set(asset_lock "${asset_destination}.lock")
if (IS_SYMLINK "${asset_lock}" OR IS_DIRECTORY "${asset_lock}")
    message(FATAL_ERROR "Invalid runtime asset staging lock: ${asset_lock}")
endif ()
file(LOCK "${asset_lock}" GUARD PROCESS)
foreach (category IN ITEMS {{runtime_categories}})
    if (IS_SYMLINK "${asset_source}/${category}" OR IS_SYMLINK "${asset_destination}/${category}")
        message(FATAL_ERROR "Runtime asset category uses a symlink: ${category}")
    endif ()
    if (IS_DIRECTORY "${asset_destination}/${category}")
        _ecosystem_prune_asset_caches("${asset_destination}/${category}")
    endif ()
endforeach ()
_ecosystem_runtime_asset_files("${asset_source}" ecosystem_asset_files)
foreach (source IN LISTS ecosystem_asset_files)
    file(RELATIVE_PATH relative "${asset_source}" "${source}")
    get_filename_component(parent "${relative}" DIRECTORY)
    set(destination "${asset_destination}")
    string(REPLACE "/" ";" parents "${parent}")
    foreach (part IN LISTS parents)
        set(destination "${destination}/${part}")
        if (IS_SYMLINK "${destination}" OR
                (EXISTS "${destination}" AND NOT IS_DIRECTORY "${destination}"))
            message(FATAL_ERROR "Invalid runtime asset destination: ${destination}")
        endif ()
    endforeach ()
    if (IS_SYMLINK "${asset_destination}/${relative}")
        message(FATAL_ERROR "Runtime asset destination uses a symlink: ${relative}")
    endif ()
    # Materialize safe source links; the payload must not depend on source paths.
    configure_file("${source}" "${asset_destination}/${relative}" COPYONLY)
endforeach ()
# Retire media staged by older facades without following a directory alias.
if (IS_SYMLINK "${asset_destination}/showcase")
    file(REMOVE "${asset_destination}/showcase")
else ()
    file(REMOVE_RECURSE "${asset_destination}/showcase")
endif ()
