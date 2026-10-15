# tests/CMakeLists.txt

add_executable(test_unit
  unit/test_version.cpp
  unit/test_grammars.cpp
  unit/test_cypher_params.cpp
  unit/test_result_parse.cpp
  unit/test_python_extractor.cpp
  unit/test_ts_extractor.cpp
  unit/test_js_extractor.cpp
  unit/test_syntax.cpp
  unit/test_metrics.cpp
  unit/test_binding.cpp
)
target_link_libraries(test_unit PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_unit)

add_test(NAME test_unit COMMAND test_unit)
set_tests_properties(test_unit PROPERTIES LABELS "unit")

add_executable(test_paths unit/test_paths.cpp)
target_link_libraries(test_paths PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_paths)
add_test(NAME test_paths COMMAND test_paths)
set_tests_properties(test_paths PROPERTIES LABELS "unit")

add_executable(test_scanner unit/test_scanner.cpp)
target_link_libraries(test_scanner PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_scanner)
add_test(NAME test_scanner COMMAND test_scanner)
set_tests_properties(test_scanner PROPERTIES LABELS "unit")

add_executable(test_fact_store unit/test_fact_store.cpp)
target_link_libraries(test_fact_store PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_fact_store)
add_test(NAME test_fact_store COMMAND test_fact_store)
set_tests_properties(test_fact_store PROPERTIES LABELS "unit")

add_executable(test_resolver unit/test_resolver.cpp)
target_link_libraries(test_resolver PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_resolver)
add_test(NAME test_resolver COMMAND test_resolver)
set_tests_properties(test_resolver PROPERTIES LABELS "unit")

add_executable(test_adversarial_m3a unit/test_adversarial_m3a.cpp)
target_link_libraries(test_adversarial_m3a PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_adversarial_m3a)
add_test(NAME test_adversarial_m3a COMMAND test_adversarial_m3a)
set_tests_properties(test_adversarial_m3a PROPERTIES LABELS "unit")

add_executable(test_integration integration/test_graph_client.cpp)
target_link_libraries(test_integration PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_integration)

add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES LABELS "db")

add_executable(test_graph_writer integration/test_graph_writer.cpp)
target_link_libraries(test_graph_writer PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_graph_writer)

add_test(NAME test_graph_writer COMMAND test_graph_writer)
set_tests_properties(test_graph_writer PROPERTIES LABELS "db")


