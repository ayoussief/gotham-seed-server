# Tor Wrapper Library
cmake_minimum_required(VERSION 3.28.3)

project(TorWrapper VERSION 1.0.0 LANGUAGES C CXX)

# Create the library
add_library(${PROJECT_NAME} STATIC
    src/tor_service.cpp
    src/onion_identity_manager.cpp
    src/gotham_peer_connector.cpp
    src/gotham_tor_mesh.cpp
    src/gotham_protocol.cpp
)

# Set up Tor library paths
set(TOR_ROOT_DIR ${CMAKE_SOURCE_DIR}/src/tor)
set(TOR_INCLUDE_DIRS 
    ${TOR_ROOT_DIR}/src
    ${TOR_ROOT_DIR}/src/feature/api
    ${TOR_ROOT_DIR}/src/lib
    ${TOR_ROOT_DIR}/src/core
    ${TOR_ROOT_DIR}/src/ext
    ${TOR_ROOT_DIR}/src/ext/trunnel
    ${TOR_ROOT_DIR}/src/trunnel
    ${TOR_ROOT_DIR}
)

# Find all Tor static libraries (excluding testing libraries and duplicates)
file(GLOB_RECURSE TOR_ALL_LIBRARIES "${TOR_ROOT_DIR}/src/**/*.a")
set(TOR_LIBRARIES)
foreach(lib ${TOR_ALL_LIBRARIES})
    if(NOT ${lib} MATCHES "-testing\\.a$" AND 
       NOT ${lib} MATCHES "libtorrunner\\.a$" AND
       NOT ${lib} MATCHES "libtor-gencert\\.a$")
        list(APPEND TOR_LIBRARIES ${lib})
    endif()
endforeach()

# Find required packages for Tor
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(LZMA REQUIRED liblzma)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

# Public include directories (what users of this library see)
target_include_directories(${PROJECT_NAME} 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${TOR_INCLUDE_DIRS}
        ${LIBEVENT_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${ZSTD_INCLUDE_DIRS}
        ${LZMA_INCLUDE_DIRS}
        ${SYSTEMD_INCLUDE_DIRS}
)

# Link Tor and its dependencies
target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        # Use --whole-archive to ensure all Tor symbols are included
        -Wl,--whole-archive
        ${TOR_LIBRARIES}
        -Wl,--no-whole-archive
        
        # System and external libraries
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${LZMA_LIBRARIES}
        ${SYSTEMD_LIBRARIES}
        m  # Math library
        dl # Dynamic linking library
)

# Set C++ standard
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# Define macros needed by Tor
target_compile_definitions(${PROJECT_NAME} PRIVATE TOR_UNIT_TESTS)

# Set library properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "tor-wrapper"
)