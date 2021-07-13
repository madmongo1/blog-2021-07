add_subdirectory(src)

add_executable(webserver-cxx20 cxx20.cpp)
target_link_libraries(webserver-cxx20 PUBLIC webserver-cxx20-src)

target_compile_features(webserver-cxx20 PUBLIC cxx_std_20)

## program_stop
add_executable(test-program_stop test-program_stop.cpp)
target_link_libraries(test-program_stop PUBLIC webserver-cxx20-src)

## interrupt_or_wait
add_executable(test-interrupt_or_wait test-interrupt_or_wait.cpp)
target_link_libraries(test-interrupt_or_wait PUBLIC webserver-cxx20-src)
