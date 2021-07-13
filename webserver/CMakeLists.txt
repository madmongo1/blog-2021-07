add_subdirectory(src)

add_executable(webserver-cxx20 cxx20.cpp)
target_link_libraries(webserver-cxx20 PUBLIC webserver-cxx20-src)

target_compile_features(webserver-cxx20 PUBLIC cxx_std_20)

## program_stop
add_executable(program_stop program_stop.cpp)
target_link_libraries(program_stop PUBLIC webserver-cxx20-src)

## interrupt_or_wait
add_executable(interrupt_or_wait interrupt_or_wait.cpp)
target_link_libraries(interrupt_or_wait PUBLIC webserver-cxx20-src)
