add_executable(woodyn_bench main.cpp)
target_link_libraries(woodyn_bench PRIVATE woodyn::engine)
woodyn_set_warnings(woodyn_bench)
