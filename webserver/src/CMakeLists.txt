file(GLOB_RECURSE websocket_cxx20_src_files CONFIGURE_DEPENDS "*.hpp" "*.cpp")

add_library(webserver-cxx20-src OBJECT ${websocket_cxx20_src_files})
target_include_directories(webserver-cxx20-src PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(webserver-cxx20-src 
    PUBLIC 
        Boost::boost
        OpenSSL::SSL OpenSSL::Crypto
        Threads::Threads)
