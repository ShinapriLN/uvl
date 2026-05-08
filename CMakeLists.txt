cmake_minimum_required(VERSION 3.16)

project(uvl LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(OPENSSL REQUIRED openssl)


file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/uvl/*.c"
)
list(REMOVE_ITEM SOURCES
    "src/uvl/fuse.c"
)
add_executable(uvl ${SOURCES})
target_include_directories(uvl PUBLIC
    include
)
target_include_directories(uvl PRIVATE 
    ${FUSE3_INCLUDE_DIRS} 
    ${OPENSSL_INCLUDE_DIRS}
)
target_compile_definitions(uvl PRIVATE 
    _DEFAULT_SOURCE 
    _XOPEN_SOURCE=700
)
target_compile_options(uvl PRIVATE
    -Wall
    -Wextra
    -Wno-deprecated-declarations
    -Wno-format-truncation
    ${FUSE3_CFLAGS_OTHER}
    ${OPENSSL_CFLAGS_OTHER}
)
target_link_libraries(uvl PRIVATE 
    ${FUSE3_LIBRARIES} 
    ${OPENSSL_LIBRARIES}
)
target_link_directories(uvl PRIVATE 
    src/
    ${FUSE3_LIBRARY_DIRS} 
    ${OPENSSL_LIBRARY_DIRS}
)

install(TARGETS uvl RUNTIME DESTINATION bin)
