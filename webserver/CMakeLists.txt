add_subdirectory(src)

add_executable(webserver webserver.cpp)
target_link_libraries(webserver PUBLIC webserver-cxx20-src)
target_compile_features(webserver PUBLIC cxx_std_20)

## program_stop
add_executable(program_stop program_stop.cpp)
target_link_libraries(program_stop PUBLIC webserver-cxx20-src)
target_compile_features(program_stop PUBLIC cxx_std_20)

## interrupt_or_wait
add_executable(interrupt_or_wait interrupt_or_wait.cpp)
target_link_libraries(interrupt_or_wait PUBLIC webserver-cxx20-src)
target_compile_features(interrupt_or_wait PUBLIC cxx_std_20)

## interrupt_or_wait
add_executable(wsecho wsecho.cpp)
target_link_libraries(wsecho PUBLIC webserver-cxx20-src)
target_compile_features(wsecho PUBLIC cxx_std_20)
