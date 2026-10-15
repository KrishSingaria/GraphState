include(FetchContent)
find_package(Threads REQUIRED)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# tree-sitter core
set(AMALGAMATED ON CACHE BOOL "" FORCE)
FetchContent_Declare(treesitter GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
                     GIT_TAG v0.27.1 GIT_SHALLOW TRUE)
# hiredis
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(hiredis GIT_REPOSITORY https://github.com/redis/hiredis.git GIT_TAG v1.4.1 GIT_SHALLOW TRUE)
# json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
# xxhash (download only)
FetchContent_Declare(xxhash GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git GIT_TAG v0.8.4
                     GIT_SHALLOW TRUE SOURCE_SUBDIR _none_)
FetchContent_MakeAvailable(treesitter hiredis json xxhash)

if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
  add_library(hiredis::hiredis ALIAS hiredis)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/hiredis")
file(WRITE "${CMAKE_BINARY_DIR}/generated/hiredis/hiredis.h" "#pragma once\n#include \"${hiredis_SOURCE_DIR}/hiredis.h\"\n")
file(WRITE "${CMAKE_BINARY_DIR}/generated/hiredis/async.h" "#pragma once\n#include \"${hiredis_SOURCE_DIR}/async.h\"\n")
file(WRITE "${CMAKE_BINARY_DIR}/generated/hiredis/read.h" "#pragma once\n#include \"${hiredis_SOURCE_DIR}/read.h\"\n")
file(WRITE "${CMAKE_BINARY_DIR}/generated/hiredis/sds.h" "#pragma once\n#include \"${hiredis_SOURCE_DIR}/sds.h\"\n")

if(GS_BUILD_TESTS)
  FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(doctest)
endif()
