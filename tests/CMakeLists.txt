# tests/CMakeLists.txt

add_executable(test_unit unit/test_version.cpp unit/test_grammars.cpp unit/test_cypher_params.cpp unit/test_result_parse.cpp)
target_link_libraries(test_unit PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_unit)

add_test(NAME test_unit COMMAND test_unit)
set_tests_properties(test_unit PROPERTIES LABELS "unit")

add_executable(test_integration integration/test_graph_client.cpp)
target_link_libraries(test_integration PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_integration)

add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES LABELS "db")

