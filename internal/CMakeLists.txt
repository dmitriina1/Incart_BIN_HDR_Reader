cmake_minimum_required(VERSION 3.20)
project(signal_internal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(HDR_FILE "" CACHE FILEPATH "Path to a .hdr file used by the HDR reader test")

add_library(sig_common STATIC common/signal_common.cpp)
target_include_directories(sig_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(sig_hdr STATIC hdr/hdr_reader.cpp)
target_include_directories(sig_hdr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(sig_bin STATIC bin/bin_reader.cpp)
target_include_directories(sig_bin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sig_bin PUBLIC sig_hdr sig_common)

add_library(sig_test_utils STATIC common/test_utils.cpp)
target_include_directories(sig_test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sig_test_utils PUBLIC sig_common sig_bin)

add_executable(test_hdr hdr/test_hdr_reader.cpp)
target_link_libraries(test_hdr PRIVATE sig_hdr)

add_executable(test_bin bin/test_bin_reader.cpp)
target_link_libraries(test_bin PRIVATE sig_bin sig_hdr)

find_package(DuckDB CONFIG REQUIRED)

if(TARGET duckdb)
    set(SIGNALS_DUCKDB_TARGET duckdb)
elseif(TARGET duckdb_static)
    set(SIGNALS_DUCKDB_TARGET duckdb_static)
else()
    message(FATAL_ERROR "DuckDB package did not provide duckdb or duckdb_static target")
endif()

add_library(sig_duckdb STATIC duckdb/duckdb_converter.cpp)
target_include_directories(sig_duckdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sig_duckdb PUBLIC ${SIGNALS_DUCKDB_TARGET})

add_executable(test_duckdb duckdb/test_duckdb.cpp)
target_link_libraries(test_duckdb PRIVATE sig_duckdb sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)

add_executable(duckdb_runner duckdb_runner.cpp)
target_link_libraries(duckdb_runner PRIVATE sig_duckdb sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)

find_package(HDF5 REQUIRED COMPONENTS C CXX)

add_library(sig_hdf5 STATIC hdf5/hdf5_converter.cpp)
target_include_directories(sig_hdf5 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sig_hdf5 PUBLIC ${HDF5_CXX_LIBRARIES} ${HDF5_LIBRARIES})

add_executable(test_hdf5 hdf5/test_hdf5_converter.cpp)
target_link_libraries(test_hdf5 PRIVATE sig_hdf5 sig_bin sig_hdr sig_common sig_test_utils)

add_executable(hdf5_runner hdf5_runner.cpp)
target_link_libraries(hdf5_runner PRIVATE sig_hdf5 sig_bin sig_hdr sig_common sig_test_utils)

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

add_library(sig_parquet STATIC parquet/parquet_converter.cpp)
target_include_directories(sig_parquet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sig_parquet PUBLIC
    ${SIGNALS_DUCKDB_TARGET}
    ${SIGNALS_ARROW_TARGET}
    ${SIGNALS_PARQUET_TARGET}
)

add_executable(test_parquet parquet/test_parquet_converter.cpp)
if(MINGW)
    target_link_options(test_parquet PRIVATE -mconsole)
endif()
target_link_libraries(test_parquet PRIVATE sig_parquet sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)

add_executable(parquet_runner parquet_runner.cpp)
# target_link_options(parquet_runner PRIVATE -mconsole)
target_link_libraries(parquet_runner PRIVATE sig_parquet sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)

add_executable(test_all_formats test_all_formats.cpp)
target_link_libraries(test_all_formats PRIVATE sig_hdf5 sig_parquet sig_duckdb sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)

enable_testing()

add_executable(self_test_hdr self_tests/test_hdr.cpp)
target_link_libraries(self_test_hdr PRIVATE sig_hdr sig_test_utils)
add_test(NAME self_test_hdr COMMAND self_test_hdr)

add_executable(self_test_bin self_tests/test_bin.cpp)
target_link_libraries(self_test_bin PRIVATE sig_bin sig_hdr sig_test_utils)
add_test(NAME self_test_bin COMMAND self_test_bin)

add_executable(self_test_duckdb self_tests/test_duckdb.cpp)
target_link_libraries(self_test_duckdb PRIVATE sig_duckdb sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)
add_test(NAME self_test_duckdb COMMAND self_test_duckdb)

add_executable(self_test_hdf5 self_tests/test_hdf5.cpp)
target_link_libraries(self_test_hdf5 PRIVATE sig_hdf5 sig_bin sig_hdr sig_common sig_test_utils)
add_test(NAME self_test_hdf5 COMMAND self_test_hdf5)

add_executable(self_test_parquet self_tests/test_parquet.cpp)
target_link_libraries(self_test_parquet PRIVATE sig_parquet sig_bin sig_hdr ${SIGNALS_DUCKDB_TARGET} sig_common sig_test_utils)
add_test(NAME self_test_parquet COMMAND self_test_parquet)

# set(HDR_FILE "" CACHE FILEPATH "Path to a .hdr file used by the HDR reader test")
set(BIN_FILE "" CACHE FILEPATH "Path to a .hdr or .bin file used by the BIN reader test")
set(DUCKDB_FILE "" CACHE FILEPATH "Path to a .hdr or .bin file used by the DuckDB streaming test")
set(HDF5_FILE "" CACHE FILEPATH "Path to a .hdr or .bin file used by the HDF5 converter test")
set(PARQUET_FILE "" CACHE FILEPATH "Path to a .hdr or .bin file used by the Parquet converter test")
set(FORMAT_BENCH_FILE "" CACHE FILEPATH "Path to a .hdr or .bin file used by the unified formats test")

if(HDR_FILE)
    add_test(NAME hdr_reader_test COMMAND test_hdr "${HDR_FILE}")
    message(STATUS "HDR test will use: ${HDR_FILE}")

    add_test(NAME bin_reader_test COMMAND test_bin "${HDR_FILE}")
    message(STATUS "BIN test will use: ${HDR_FILE}")

    add_test(NAME test_duckdb COMMAND test_duckdb "${HDR_FILE}" "0" "-1" "1000")
    message(STATUS "DuckDB streaming test will use: ${HDR_FILE}")

    add_test(NAME hdf5_converter_test COMMAND test_hdf5 "${HDR_FILE}" "0" "-1" "1000")
    message(STATUS "HDF5 test will use: ${HDR_FILE}")

    add_test(NAME parquet_converter_test COMMAND test_parquet "${HDR_FILE}" "0" "-1" "1000")
    message(STATUS "Parquet test will use: ${HDR_FILE}")

endif()