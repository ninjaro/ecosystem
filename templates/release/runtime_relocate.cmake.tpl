cmake_minimum_required(VERSION 3.20)
set(files
{{files}})
foreach(payload IN LISTS files)
    file(RPATH_REMOVE FILE "${payload}")
endforeach()
