cmake_minimum_required(VERSION 3.15)
project(simplehdlc C)

set(CMAKE_C_STANDARD 99)

include_directories(. tests/cmocka/include)

add_executable(simplehdlc hdlc.c hdlc.h hdlc_crc32.h hdlc_crc32.c tests/main.c tests/cmocka/src/cmocka.c)