cmake_minimum_required(VERSION 3.20)

project(Blog202107)

message(STATUS "BOOST_ROOT = ${BOOST_ROOT}")

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(webserver)
