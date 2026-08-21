add_headers_and_sources(k_way_merge_test ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(k_way_merge_test ${k_way_merge_test_headers} ${k_way_merge_test_sources})
target_include_directories(k_way_merge_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(k_way_merge_test PRIVATE abseil_btree)

add_test(NAME correctness COMMAND k_way_merge_test)
