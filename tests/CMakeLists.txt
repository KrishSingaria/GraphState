# tests/CMakeLists.txt

add_executable(test_unit unit/test_version.cpp unit/test_grammars.cpp)
target_link_libraries(test_unit PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_unit)

add_test(NAME test_unit COMMAND test_unit)
set_tests_properties(test_unit PROPERTIES LABELS "unit")
