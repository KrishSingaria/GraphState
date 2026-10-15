# name | repo | tag | subdir | has_scanner
set(GS_GRAMMARS
  "python|tree-sitter-python|v0.25.0||1"
  "typescript|tree-sitter-typescript|v0.23.2|typescript|1"
  "tsx|tree-sitter-typescript|v0.23.2|tsx|1"
  "javascript|tree-sitter-javascript|v0.25.0||1"
  "go|tree-sitter-go|v0.25.0||0"
  "rust|tree-sitter-rust|v0.24.2||1"
  "java|tree-sitter-java|v0.23.5||0"
  "c|tree-sitter-c|v0.24.2||0"
  "json|tree-sitter-json|v0.24.8||0")

if(GS_WITH_CPP_GRAMMAR)
  list(APPEND GS_GRAMMARS "cpp|tree-sitter-cpp|v0.23.4||1")
endif()

add_library(gs_grammars STATIC)

foreach(entry IN LISTS GS_GRAMMARS)
  string(REPLACE "|" ";" f "${entry}")
  list(GET f 0 name)
  list(GET f 1 repo)
  list(GET f 2 tag)
  list(GET f 3 sub)
  list(GET f 4 scanner)
  string(REPLACE "-" "_" fc_name "${repo}")

  if(NOT DEFINED ${fc_name}_POPULATED_DONE)
    FetchContent_Declare(${fc_name}
      GIT_REPOSITORY https://github.com/tree-sitter/${repo}.git
      GIT_TAG ${tag}
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR _none_
    )
    FetchContent_MakeAvailable(${fc_name})
    set(${fc_name}_POPULATED_DONE 1)
  endif()

  set(root "${${fc_name}_SOURCE_DIR}")
  if(sub)
    set(root "${root}/${sub}")
  endif()

  set(src_files "${root}/src/parser.c")
  if(scanner)
    if(EXISTS "${root}/src/scanner.c")
      list(APPEND src_files "${root}/src/scanner.c")
    elseif(EXISTS "${root}/src/scanner.cc")
      list(APPEND src_files "${root}/src/scanner.cc")
    endif()
  endif()

  add_library(ts_${name} OBJECT ${src_files})
  target_include_directories(ts_${name} PRIVATE ${root}/src ${${fc_name}_SOURCE_DIR})
  set_target_properties(ts_${name} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(ts_${name} PRIVATE /W0 /O2 /bigobj /utf-8)
  else()
    target_compile_options(ts_${name} PRIVATE -O2 -w)
  endif()
  target_sources(gs_grammars PRIVATE $<TARGET_OBJECTS:ts_${name}>)
endforeach()

target_link_libraries(gs_grammars PUBLIC tree-sitter)
