function(gs_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /wd4200 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Werror
      -Wno-missing-field-initializers
      -Wno-unused-parameter
      -Wno-pedantic
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -Wno-unknown-warning-option)
    endif()
  endif()
endfunction()

# static CRT on Windows -> single self-contained graphstate.exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
