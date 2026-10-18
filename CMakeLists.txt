cmake_minimum_required(VERSION 3.24)
project(graphstate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(GS_WITH_CPP_GRAMMAR "Build tree-sitter-cpp (slow compile)" ON)
option(GS_SANITIZE "ASan+UBSan for our code" OFF)
option(GS_BUILD_TESTS "Build tests" ON)

include(cmake/Flags.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Grammars.cmake)

file(GLOB_RECURSE GS_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM GS_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)
if(WIN32)
  list(FILTER GS_SOURCES EXCLUDE REGEX "platform_posix\\.cpp$")
else()
  list(FILTER GS_SOURCES EXCLUDE REGEX "platform_win\\.cpp$")
endif()

add_library(graphstate_core STATIC ${GS_SOURCES})
target_include_directories(graphstate_core PUBLIC src)
target_include_directories(graphstate_core SYSTEM PUBLIC
  ${CMAKE_BINARY_DIR}/generated
  ${xxhash_SOURCE_DIR}
  ${hiredis_SOURCE_DIR}
  ${treesitter_SOURCE_DIR}/lib/include
  ${json_SOURCE_DIR}/include
)
target_link_libraries(graphstate_core PUBLIC tree-sitter gs_grammars hiredis::hiredis nlohmann_json::nlohmann_json Threads::Threads)
gs_apply_warnings(graphstate_core)

if(WIN32)
  target_link_libraries(graphstate_core PUBLIC ws2_32)
endif()

target_compile_definitions(graphstate_core PUBLIC GS_VERSION="${PROJECT_VERSION}"
                           $<$<BOOL:${GS_WITH_CPP_GRAMMAR}>:GS_WITH_CPP_GRAMMAR=1>)

if(GS_SANITIZE)
  if(MSVC)
    target_compile_options(graphstate_core PUBLIC /fsanitize=address)
  else()
    target_compile_options(graphstate_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(graphstate_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

# embed host-kit templates + server instructions
include(cmake/Embed.cmake)
gs_embed(${CMAKE_BINARY_DIR}/generated/host_kits.inc ${CMAKE_SOURCE_DIR}/host-kits)

add_executable(graphstate src/main.cpp $<$<BOOL:${WIN32}>:${CMAKE_SOURCE_DIR}/src/platform/graphstate.manifest>)
target_link_libraries(graphstate PRIVATE graphstate_core)
gs_apply_warnings(graphstate)

install(TARGETS graphstate)

if(GS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
