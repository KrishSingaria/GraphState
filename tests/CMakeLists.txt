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

add_executable(test_text unit/test_text.cpp)
target_link_libraries(test_text PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_text)
add_test(NAME test_text COMMAND test_text)
set_tests_properties(test_text PROPERTIES LABELS "unit")

add_executable(test_schema_validate unit/test_schema_validate.cpp)
target_link_libraries(test_schema_validate PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_schema_validate)
add_test(NAME test_schema_validate COMMAND test_schema_validate)
set_tests_properties(test_schema_validate PROPERTIES LABELS "unit")

add_executable(test_apply unit/test_apply.cpp)
target_link_libraries(test_apply PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_apply)
add_test(NAME test_apply COMMAND test_apply)
set_tests_properties(test_apply PROPERTIES LABELS "unit")

add_executable(test_rules unit/test_rules.cpp)
target_link_libraries(test_rules PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_rules)
add_test(NAME test_rules COMMAND test_rules)
set_tests_properties(test_rules PROPERTIES LABELS "unit")

add_executable(test_disk_txn unit/test_disk_txn.cpp)
target_link_libraries(test_disk_txn PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_disk_txn)
add_test(NAME test_disk_txn COMMAND test_disk_txn)
set_tests_properties(test_disk_txn PROPERTIES LABELS "unit")

add_executable(test_gate_e2e integration/test_gate_e2e.cpp)
target_link_libraries(test_gate_e2e PRIVATE graphstate_core doctest::doctest)
gs_apply_warnings(test_gate_e2e)
add_test(NAME test_gate_e2e COMMAND test_gate_e2e)
set_tests_properties(test_gate_e2e PROPERTIES LABELS "db")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_mcp_e2e
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/run.py"
            --bin "$<TARGET_FILE:graphstate>"
            --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/benchmark_project"
  )
  set_tests_properties(test_mcp_e2e PROPERTIES LABELS "e2e")
endif()



