#include "converterapi.h"

#include "bin/bin_reader.hpp"
#include "common/signal_common.hpp"
#include "duckdb/duckdb_converter.hpp"
#include "hdf5/hdf5_converter.hpp"
#include "hdr/hdr_reader.hpp"
#include "parquet/parquet_converter.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <vector>

struct signalData {
	signals::SignalData value;
	std::vector<double> converted_channel_cache;
};

void free_header_vectors(signals::SignalData *data) {
	if (!data)
		return;

	data->channel_names.clear();
	data->units.clear();
}

extern "C" {
CONVERTER_C_API signalData *signal_data_create(void) {
	return new signalData{}; // NOSONAR
}

CONVERTER_C_API void signal_data_free(signalData *data) {
	if (data) {
		free_header_vectors(&data->value);
		data->converted_channel_cache.clear();
		delete data; // NOSONAR
	}
}

CONVERTER_C_API void signal_data_clear(signalData *data) {
	if (data) {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();
	}
}

CONVERTER_C_API int32_t signals_parquet_read(const char *parquet_path, signalData *data,
                                             int64_t point_off, int64_t point_cnt) {
	if (!parquet_path || !data)
		return EINVAL;

	try {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();

		signals::parquet_read_signal_data_like_stream(std::filesystem::path(parquet_path),
		                                              data->value, point_off, point_cnt);

		return 0;
	} catch (...) {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_parquet_write(const char *bin_path, const char *parquet_path,
                                              int64_t point_off, int64_t point_cnt,
                                              int64_t row_group_points) {
	if (!bin_path || !parquet_path)
		return EINVAL;
	try {
		signals::parquet_write_signal_data_like_stream(std::filesystem::path(bin_path),
		                                               std::filesystem::path(parquet_path),
		                                               point_off, point_cnt, row_group_points);
		return 0;
	} catch (...) {
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_hdf5_write(const char *bin_path, const char *hdf5_path,
                                           int64_t point_off, int64_t point_cnt) {
	if (!bin_path || !hdf5_path)
		return EINVAL;
	try {
		signals::hdf5_write_signals_data(std::filesystem::path(bin_path),
		                                 std::filesystem::path(hdf5_path), point_off, point_cnt);
		return 0;
	} catch (...) {
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_hdf5_read(const char *hdf5_path, signalData *data,
                                          int64_t point_off, int64_t point_cnt,
                                          int64_t chunk_points) {
	if (!hdf5_path || !data)
		return EINVAL;
	try {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();

		signals::hdf5_read_signal_data_like_stream(std::filesystem::path(hdf5_path), data->value,
		                                           point_off, point_cnt, chunk_points);
		return 0;
	} catch (...) {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_duckdb_read(const char *duckdb_path, signalData *data,
                                            int64_t point_off, int64_t point_cnt) {
	if (!duckdb_path || !data)
		return EINVAL;
	try {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();

		signals::duckdb_read_signal_data_like_stream(std::filesystem::path(duckdb_path),
		                                             data->value, point_off, point_cnt);
		return 0;
	} catch (...) {
		free_header_vectors(&data->value);
		data->value = signals::SignalData{};
		data->converted_channel_cache.clear();
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_duckdb_write(const char *bin_path, const char *duckdb_path,
                                             int64_t point_off, int64_t point_cnt) {
	if (!bin_path || !duckdb_path)
		return EINVAL;
	try {
		signals::duckdb_write_signal_data_like_stream(std::filesystem::path(bin_path),
		                                              std::filesystem::path(duckdb_path), point_off,
		                                              point_cnt);
		return 0;
	} catch (const std::exception &e) {
		fprintf(stderr, "DuckDB error: %s\n", e.what());
		return EIO;
	} catch (...) {
		fprintf(stderr, "DuckDB unknown error\n");
		return EIO;
	}
}

CONVERTER_C_API int32_t signals_duckdb_write_with_params(const char *bin_path,
                                                         const char *duckdb_path, int64_t point_off,
                                                         int64_t point_cnt, int64_t chunk_points,
                                                         int64_t commit_every_n_points) {
	if (!bin_path || !duckdb_path)
		return EINVAL;
	try {
		signals::duckdb_write_signal_data_like_stream(
		    std::filesystem::path(bin_path), std::filesystem::path(duckdb_path), point_off,
		    point_cnt, chunk_points, commit_every_n_points);
		return 0;
	} catch (...) {
		return EIO;
	}
}

CONVERTER_C_API int32_t signal_data_get_channel_count(const signalData *data) {
	if (!data)
		return EINVAL;
	return (int32_t)data->value.channel_names.size();
}

CONVERTER_C_API const char *signal_data_get_channel_name(const signalData *data, int32_t idx) {
	if (!data || idx < 0)
		return nullptr;
	const auto channel_num = static_cast<size_t>(idx);
	if (channel_num >= data->value.channel_names.size())
		return nullptr;
	return data->value.channel_names[channel_num].c_str();
}

CONVERTER_C_API const char *signal_data_get_unit(const signalData *data, int32_t idx) {
	if (!data || idx < 0)
		return nullptr;
	const auto unit_num = static_cast<size_t>(idx);
	if (unit_num >= data->value.units.size())
		return nullptr;
	return data->value.units[unit_num].c_str();
}

CONVERTER_C_API int64_t signal_data_get_point_count(const signalData *data) {
	if (!data)
		return EINVAL;
	return data->value.num_points;
}

CONVERTER_C_API int64_t signal_data_get_first_point(const signalData *data) {
	if (!data)
		return EINVAL;
	return data->value.ifirst_point;
}

CONVERTER_C_API int64_t signal_data_get_last_point(const signalData *data) {
	if (!data)
		return EINVAL;
	return data->value.ilast_point;
}

CONVERTER_C_API double signal_data_get_frequency(const signalData *data) {
	if (!data)
		return EINVAL;
	return data->value.frequency;
}

CONVERTER_C_API double signal_data_get_lsb_default(const signalData *data) {
	if (!data)
		return EINVAL;
	return data->value.lsb_default;
}

CONVERTER_C_API double signal_data_get_lsb(const signalData *data, int32_t channel_num) {
	if (!data || channel_num < 0 || channel_num >= data->value.num_channels)
		return EINVAL;

	// S6004 - сокращение области видимости переменных для предотвращения ошибок и улучшения
	// читаемости
	if (const auto idx = static_cast<size_t>(channel_num); idx < data->value.lsbs.size()) {
		return data->value.lsbs[idx];
	}
	return data->value.lsb_default;
}

CONVERTER_C_API const double *signal_data_get_scaled_channel_values(signalData *data,
                                                                    int32_t channel_num) {
	if (!data || channel_num < 0)
		return nullptr;

	if (static_cast<size_t>(channel_num) >= data->value.channel_names.size()) {
		return nullptr;
	}

	const int64_t num_points = data->value.num_points;
	data->converted_channel_cache.resize(static_cast<size_t>(num_points));

	for (int64_t i = 0; i < num_points; i++) {
		data->converted_channel_cache[static_cast<size_t>(i)] = data->value.value(channel_num, i);
	}

	return data->converted_channel_cache.data();
}

CONVERTER_C_API const double *signal_data_get_channel_values(signalData *data,
                                                             int32_t channel_num) {
	return signal_data_get_scaled_channel_values(data, channel_num);
}

CONVERTER_C_API const int32_t *signal_data_get_raw_channel_values(const signalData *data,
                                                                  int32_t channel_num) {
	if (!data || channel_num < 0 || channel_num >= data->value.num_channels)
		return nullptr;

	return data->value.raw_channel(channel_num);
}
}