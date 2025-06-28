cmake_minimum_required(VERSION 3.28.3)

project(GothamSeedServer VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(LZMA REQUIRED liblzma)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

# Set up Tor library paths
set(TOR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tor)
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

# Include directories
include_directories(
    include
    src/tor-wrapper/include
    ${TOR_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/main.cpp
    src/seed_server.cpp
    src/peer_manager.cpp
    src/gcty_handler.cpp
    src/tor_manager.cpp
    src/tor-wrapper/src/tor_service.cpp  # Tor wrapper service
    src/gcty_protocol.cpp  # Self-contained protocol implementation
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Set output name
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "gotham-seed-server")

# Link libraries
target_link_libraries(${PROJECT_NAME}
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
    pthread
    dl
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE 
    -Wall -Wextra -Wpedantic
    -O2
    -DNDEBUG
)

# Include directories for linking
target_include_directories(${PROJECT_NAME} PRIVATE
    ${TOR_INCLUDE_DIRS}
    ${LIBEVENT_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZMA_INCLUDE_DIRS}
    ${SYSTEMD_INCLUDE_DIRS}
)

# Define macros needed by Tor
target_compile_definitions(${PROJECT_NAME} PRIVATE TOR_UNIT_TESTS)

# Install target
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Install configuration files
install(FILES
    config/seed-server.conf.example
    DESTINATION etc/gotham-seed-server
)

# Create systemd service file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/systemd/gotham-seed-server.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/gotham-seed-server.service
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gotham-seed-server.service
    DESTINATION lib/systemd/system
)