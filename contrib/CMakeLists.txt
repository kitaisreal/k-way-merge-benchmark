# Add Abseil b-tree library

set(BUILD_TESTING OFF)
set(ABSL_PROPAGATE_CXX_STD ON)

add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)
add_library(abseil_btree INTERFACE)

target_link_libraries(abseil_btree INTERFACE absl::btree)

get_target_property(ABSL_BTREE_INCLUDE_DIR absl::btree INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(abseil_btree SYSTEM BEFORE INTERFACE ${ABSL_BTREE_INCLUDE_DIR})
