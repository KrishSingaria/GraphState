function(gs_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /wd4200 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-error=pedantic -Wno-gnu-flexible-array-member -Wno-c99-extensions)
  endif()
endfunction()

# static CRT on Windows -> single self-contained graphstate.exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
