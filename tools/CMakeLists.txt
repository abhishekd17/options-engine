# Shared flag parsing (header-only), also used by the tests.
add_library(opt_tool_args INTERFACE)
target_include_directories(opt_tool_args INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(opt_info opt_info.cpp)
target_link_libraries(opt_info PRIVATE opt::core)
opteng_configure_target(opt_info)

add_executable(iv_accuracy iv_accuracy.cpp)
target_link_libraries(iv_accuracy PRIVATE opt::pricing opt_tool_args)
opteng_configure_target(iv_accuracy)
