#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef CONVERTER_C_API
#if defined(_WIN32)
#if defined(CONVERTER_C_API_BUILD)
#define CONVERTER_C_API __declspec(dllexport)
#else
#define CONVERTER_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CONVERTER_C_API __attribute__((visibility("default")))
#else
#define CONVERTER_C_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct signalData;

CONVERTER_C_API struct signalData *signal_data_create(void);
CONVERTER_C_API void signal_data_free(struct signalData *data);
CONVERTER_C_API void signal_data_clear(struct signalData *data);

CONVERTER_C_API int32_t signals_parquet_read(const char *parquet_path, struct signalData *data,
                                             int64_t point_off, int64_t point_cnt);
CONVERTER_C_API int32_t signals_parquet_write(const char *bin_path, const char *parquet_path,
                                              int64_t point_off, int64_t point_cnt,
                                              int64_t row_group_points);

CONVERTER_C_API int32_t signals_hdf5_write(const char *bin_path, const char *hdf5_path,
                                           int64_t point_off, int64_t point_cnt);
CONVERTER_C_API int32_t signals_hdf5_read(const char *hdf5_path, struct signalData *data,
                                          int64_t point_off, int64_t point_cnt,
                                          int64_t chunk_points);

CONVERTER_C_API int32_t signals_duckdb_read(const char *duckdb_path, struct signalData *data,
                                            int64_t point_off, int64_t point_cnt);
CONVERTER_C_API int32_t signals_duckdb_write(const char *bin_path, const char *duckdb_path,
                                             int64_t point_off, int64_t point_cnt);
CONVERTER_C_API int32_t signals_duckdb_write_with_params(const char *bin_path,
                                                         const char *duckdb_path, int64_t point_off,
                                                         int64_t point_cnt, int64_t chunk_points,
                                                         int64_t commit_every_n_points);

CONVERTER_C_API int32_t signal_data_get_channel_count(const struct signalData *data);
CONVERTER_C_API const char *signal_data_get_channel_name(const struct signalData *data,
                                                         int32_t idx);
CONVERTER_C_API const char *signal_data_get_unit(const struct signalData *data, int32_t idx);

CONVERTER_C_API int64_t signal_data_get_point_count(const struct signalData *data);
CONVERTER_C_API int64_t signal_data_get_first_point(const struct signalData *data);
CONVERTER_C_API int64_t signal_data_get_last_point(const struct signalData *data);
CONVERTER_C_API double signal_data_get_frequency(const struct signalData *data);
CONVERTER_C_API double signal_data_get_lsb_default(const struct signalData *data);
CONVERTER_C_API double signal_data_get_lsb(const struct signalData *data, int32_t channel_num);
CONVERTER_C_API const double *signal_data_get_channel_values(struct signalData *data,
                                                             int32_t channel_num);
CONVERTER_C_API const double *signal_data_get_scaled_channel_values(struct signalData *data,
                                                                    int32_t channel_num);
CONVERTER_C_API const int32_t *signal_data_get_raw_channel_values(const struct signalData *data,
                                                                  int32_t channel_num);
#ifdef __cplusplus
}
#endif