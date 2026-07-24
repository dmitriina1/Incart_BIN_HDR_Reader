cmake_minimum_required(VERSION 3.20)
project(signals_converter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(TARGET_NAME signals_converter)

option(SIGNALS_STATIC_DEPS "Enable static linking for dependencies" ON)
option(SIGNALS_BUILD_INTERNAL "Build internal tests and runners" ON)

add_library(${TARGET_NAME} SHARED
    internal/converterapi/converterapi.cpp
    internal/common/signal_common.cpp
    internal/hdr/hdr_reader.cpp
    internal/bin/bin_reader.cpp
    internal/duckdb/duckdb_converter.cpp
    internal/hdf5/hdf5_converter.cpp
    internal/parquet/parquet_converter.cpp
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    CONVERTER_C_API_BUILD
    DUCKDB_STATIC_BUILD
    DUCKDB_EXTENSION_JSON
    DUCKDB_EXTENSION_PARQUET
    DUCKDB_EXTENSION_CORE_FUNCTIONS
)

set_target_properties(${TARGET_NAME} PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    VISIBILITY_INLINES_HIDDEN OFF
)

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/internal)

if(SIGNALS_STATIC_DEPS)
    set(DuckDB_USE_STATIC_LIBS ON)
    set(Arrow_USE_STATIC_LIBS ON)
    set(Parquet_USE_STATIC_LIBS ON)
    set(ZLIB_USE_STATIC_LIBS ON)
    set(Brotli_USE_STATIC_LIBS ON)
    set(LZ4_USE_STATIC_LIBS ON)
    set(Zstd_USE_STATIC_LIBS ON)
endif()

find_package(DuckDB CONFIG REQUIRED)

find_package(OpenSSL REQUIRED)
if(NOT TARGET OpenSSL::Crypto)
    find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto libcrypto REQUIRED)
    find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h REQUIRED)
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
    )
endif()

if(NOT TARGET OpenSSL::SSL)
    find_library(OPENSSL_SSL_LIBRARY NAMES ssl libssl REQUIRED)
    find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h REQUIRED)
    add_library(OpenSSL::SSL UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
    )
endif()

find_package(Arrow CONFIG REQUIRED)
find_package(Parquet CONFIG REQUIRED)

find_package(HDF5 REQUIRED COMPONENTS C CXX)

if(TARGET duckdb)
    set(SIGNALS_DUCKDB_TARGET duckdb)
elseif(TARGET duckdb_static)
    set(SIGNALS_DUCKDB_TARGET duckdb_static)
else()
    message(FATAL_ERROR "DuckDB package did not provide duckdb or duckdb_static target")
endif()

if(TARGET Arrow::arrow_static)
    set(SIGNALS_ARROW_TARGET Arrow::arrow_static)
elseif(TARGET Arrow::arrow_shared)
    set(SIGNALS_ARROW_TARGET Arrow::arrow_shared)
else()
    message(FATAL_ERROR "Arrow package did not provide arrow_static or arrow_shared target")
endif()

if(TARGET Parquet::parquet_static)
    set(SIGNALS_PARQUET_TARGET Parquet::parquet_static)
elseif(TARGET Parquet::parquet_shared)
    set(SIGNALS_PARQUET_TARGET Parquet::parquet_shared)
else()
    message(FATAL_ERROR "Parquet package did not provide parquet_static or parquet_shared target")
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    ${SIGNALS_DUCKDB_TARGET}
    ${HDF5_CXX_LIBRARIES}
    ${HDF5_LIBRARIES}
    ${SIGNALS_ARROW_TARGET}
    ${SIGNALS_PARQUET_TARGET}
)

if(MINGW)
    target_link_options(${TARGET_NAME} PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic
        -lpthread
        -Wl,-Bdynamic
        -Wl,--allow-multiple-definition
    )
endif()

message(STATUS "DuckDB target: ${SIGNALS_DUCKDB_TARGET}")
message(STATUS "Arrow target: ${SIGNALS_ARROW_TARGET}")
message(STATUS "Parquet target: ${SIGNALS_PARQUET_TARGET}")

if(SIGNALS_BUILD_INTERNAL)
    enable_testing()
    add_subdirectory(internal)
endif()