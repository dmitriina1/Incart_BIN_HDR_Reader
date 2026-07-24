#include "bin/bin_reader.hpp"
#include "common/signal_common.hpp"
#include "common/test_utils.hpp"
#include "duckdb/duckdb_converter.hpp"
#include "hdf5/hdf5_converter.hpp"
#include "parquet/parquet_converter.hpp"

#include <H5Cpp.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

void release_signal_data(signals::SignalData &data) {
	std::vector<int32_t>().swap(data.storage);
}

bool is_integer_arg(const char *arg) {
	if (!arg || *arg == '\0')
		return false;
	const char *cursor = arg;
	if (*cursor == '+' || *cursor == '-')
		cursor++;
	if (*cursor == '\0')
		return false;
	for (; *cursor; cursor++)
		if (!std::isdigit(static_cast<unsigned char>(*cursor)))
			return false;
	return true;
}

std::string normalize_format_filter(std::string value) {
	std::ranges::transform(value, value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (value.empty())
		return "all";
	if (value == "all" || value == "hdf5" || value == "duckdb" || value == "parquet")
		return value;
	throw std::runtime_error(
	    std::format("Неизвестный формат: {}. Ожидалось: all, hdf5, duckdb или parquet.", value));
}

std::filesystem::path to_base_path(const std::filesystem::path &source_path) {
	if (const std::string ext = source_path.extension().string(); ext == ".bin" || ext == ".hdr") {
		auto base = source_path;
		base.replace_extension("");
		return base;
	}
	return source_path;
}

void print_chunk_info(double seconds, const signals::SignalData &reference, int64_t chunk_points) {
	if (reference.num_points <= 0 || reference.num_channels <= 0) {
		std::printf("0 чанков, 0.000 сек\n");
		return;
	}
	const int64_t effective_chunk_points = (chunk_points > 0) ? chunk_points : reference.num_points;
	const int64_t chunk_count =
	    (reference.num_points + effective_chunk_points - 1) / effective_chunk_points;
	const double samples_per_second = (seconds > 0.0)
	                                      ? (static_cast<double>(reference.num_points) *
	                                         static_cast<double>(reference.num_channels) / seconds)
	                                      : 0.0;
	std::printf("%lld чанков, %.3f сек (%.1f отсчетов/сек)\n", static_cast<long long>(chunk_count),
	            seconds, samples_per_second);
}

bool run_format(const char *name, const std::function<void()> &write_fn,
                const std::function<void(signals::SignalData &)> &read_full_fn,
                const std::function<void(signals::SignalData &, int64_t, int64_t)> &read_chunk_fn,
                const signals::SignalData &reference, int64_t chunk_points,
                const std::function<uintmax_t()> &get_output_size, uintmax_t original_size_bytes) {
	bool out_ok = false;
	try {
		const double t_write = signals::lead_time(write_fn);

		signals::SignalData loaded;
		const double t_read = signals::lead_time([&read_full_fn, &loaded]() {
			read_full_fn(loaded);
		});

		const double t_seq = signals::test_serial_chunk_reading(
		    [&read_chunk_fn](const std::filesystem::path &, signals::SignalData &data, int64_t off,
		                     int64_t cnt) {
			    read_chunk_fn(data, off, cnt);
		    },
		    "", reference, chunk_points);
		const double t_rnd = signals::test_random_chunk_reading(
		    [&read_chunk_fn](const std::filesystem::path &, signals::SignalData &data, int64_t off,
		                     int64_t cnt) {
			    read_chunk_fn(data, off, cnt);
		    },
		    "", reference, chunk_points);

		const bool chunk_ok = signals::validate_serial_chunk_reading(
		    [&read_chunk_fn](const std::filesystem::path &, signals::SignalData &data, int64_t off,
		                     int64_t cnt) {
			    read_chunk_fn(data, off, cnt);
		    },
		    "", reference, chunk_points);

		const bool full_ok = signals::compare_signal_data(reference, loaded, name);

		std::vector<int32_t>().swap(loaded.storage);

		const double size_mb = static_cast<double>(get_output_size()) / (1024.0 * 1024.0);
		const double comp_pct = (original_size_bytes > 0)
		                            ? 100.0 * (1.0 - (static_cast<double>(get_output_size()) /
		                                              static_cast<double>(original_size_bytes)))
		                            : 0.0;

		std::printf("%-8s\nwrite: %8.3f s\nread: %8.3f s\nsize: %8.2f "
		            "MB\ncompression: %7.2f%%\ncompare: %s\n",
		            name, t_write, t_read, size_mb, comp_pct,
		            (full_ok && chunk_ok) ? "OK" : "MISMATCH");
		std::printf("Послед. чтение: ");
		print_chunk_info(t_seq, reference, chunk_points);
		std::printf("Случайн. чтение: ");
		print_chunk_info(t_rnd, reference, chunk_points);
		std::printf("\n");

		out_ok = full_ok && chunk_ok;
	} catch (const std::exception &e) {
		std::printf("%-8s ERROR: %s\n\n", name, e.what());
	}
	return out_ok;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::fprintf(stderr,
		             "Usage: %s <file.bin|file.hdr> [offset] [count] "
		             "[chunk_points] [format]\n",
		             argv[0]);
		std::fprintf(stderr, "   or: %s <file.bin|file.hdr> [format]\n", argv[0]);
		std::fprintf(stderr, "  format: all|hdf5|duckdb|parquet (default: all)\n");
		return 1;
	}

	const std::filesystem::path source_path(argv[1]);
	int64_t point_offset = 0;
	int64_t point_count = -1;
	int64_t chunk_points = 100000;
	std::string format_filter = "all";

	std::vector<std::string> args;
	args.reserve(static_cast<size_t>(argc));
	for (int i = 2; i < argc; i++)
		args.emplace_back(argv[i]);

	if (!args.empty()) {
		if (is_integer_arg(args[0].c_str()))
			point_offset = std::strtoll(args[0].c_str(), nullptr, 10);
		else
			format_filter = normalize_format_filter(args[0]);
	}
	if (args.size() >= 2 && is_integer_arg(args[0].c_str()))
		point_count = std::strtoll(args[1].c_str(), nullptr, 10);
	if (args.size() >= 3 && is_integer_arg(args[0].c_str()))
		chunk_points = std::strtoll(args[2].c_str(), nullptr, 10);
	if (args.size() >= 4 && is_integer_arg(args[0].c_str()))
		format_filter = normalize_format_filter(args[3]);
	if (chunk_points <= 0)
		chunk_points = 1000000;

	const std::filesystem::path base_path = to_base_path(source_path);
	std::filesystem::path bin_path = base_path;
	bin_path.replace_extension(".bin");
	std::filesystem::path header_path = base_path;
	header_path.replace_extension(".hdr");

	if (!std::filesystem::exists(bin_path) || !std::filesystem::exists(header_path)) {
		std::fprintf(stderr, "Пара BIN/HDR не найдена: %s и %s\n", bin_path.string().c_str(),
		             header_path.string().c_str());
		return 1;
	}

	const uintmax_t bin_size = std::filesystem::file_size(bin_path);
	const uintmax_t header_size = std::filesystem::file_size(header_path);
	const uintmax_t original_size = bin_size + header_size;

	signals::SignalData reference;
	try {
		const double t_bin =
		    signals::lead_time([&source_path, &point_offset, &point_count, &reference]() {
			    signals::bin_read(source_path, point_offset, point_count, reference);
		    });

		std::printf("Файл: %s\n", source_path.string().c_str());
		std::printf("Диапазон: смещение=%lld количество точек=%lld количество "
		            "точек в чанке=%lld\n",
		            static_cast<long long>(point_offset), static_cast<long long>(point_count),
		            static_cast<long long>(chunk_points));
		std::printf("BIN+HDR размер: %.2f MB\n",
		            static_cast<double>(original_size) / (1024.0 * 1024.0));
		std::printf("BIN для сравнения: %.3f s\n\n", t_bin);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "Чтение BIN файла для сравения не удалось: %s\n", e.what());
		return 1;
	}

	const bool run_hdf5 = (format_filter == "all" || format_filter == "hdf5");
	const bool run_duckdb = (format_filter == "all" || format_filter == "duckdb");
	const bool run_parquet = (format_filter == "all" || format_filter == "parquet");

	bool all_ok = true;
	bool ok;

	if (run_hdf5) {
		std::filesystem::path hdf5_path = source_path.filename();
		hdf5_path.replace_extension(".h5");
		if (std::filesystem::exists(hdf5_path))
			std::filesystem::remove(hdf5_path);

		std::unique_ptr<H5::H5File> hdf5_file;
		auto hdf5_close = [&hdf5_file]() {
			if (hdf5_file) {
				try {
					hdf5_file->close();
				} catch (const H5::FileIException &e) {
					std::fprintf(stderr, "Ошибка закрытия HDF5: %s\n", e.getCDetailMsg());
				} catch (const H5::Exception &e) {
					std::fprintf(stderr, "Неизвестная ошибка HDF5 при закрытии: %s\n",
					             e.getCDetailMsg());
				}
			}
			hdf5_file.reset();
		};

		try {
			ok = run_format(
			    "HDF5",
			    [&source_path, &hdf5_path, &point_offset, &point_count, &hdf5_file]() {
				    signals::hdf5_write_signals_data(source_path, hdf5_path, point_offset,
				                                     point_count);

				    hdf5_file = std::make_unique<H5::H5File>(H5std_string(hdf5_path.string()),
				                                             H5F_ACC_RDONLY);
			    },
			    [&hdf5_file, &point_offset, &point_count,
			     &chunk_points](signals::SignalData &data) {
				    signals::hdf5_read_signal_data_like_stream(*hdf5_file, data, point_offset,
				                                               point_count, chunk_points);
			    },
			    [&hdf5_file, &chunk_points](signals::SignalData &data, int64_t off, int64_t cnt) {
				    signals::hdf5_read_signal_data_like_stream(*hdf5_file, data, off, cnt,
				                                               chunk_points);
			    },
			    reference, chunk_points,
			    [&hdf5_path]() {
				    return std::filesystem::file_size(hdf5_path);
			    },
			    original_size);

			all_ok = all_ok && ok;
		} catch (...) {
			hdf5_close();
			throw;
		}

		hdf5_close();
	}

	if (run_parquet) {
		std::filesystem::path parquet_path = source_path.filename();
		parquet_path.replace_extension(".parquet");

		if (std::filesystem::exists(parquet_path))
			std::filesystem::remove(parquet_path);

		signals::ParquetReader parquet_reader;

		ok = run_format(
		    "Parquet",
		    [&source_path, &parquet_path, &point_offset, &point_count, &chunk_points,
		     &parquet_reader]() {
			    auto st = signals::parquet_write_signals_data(
			        source_path, parquet_path, point_offset, point_count, chunk_points);
			    if (!st.ok())
				    throw std::runtime_error(st.ToString());

			    st = signals::parquet_open(parquet_path, parquet_reader);
			    if (!st.ok())
				    throw std::runtime_error(st.ToString());
		    },
		    [&parquet_reader, &point_offset, &point_count](signals::SignalData &data) {
			    auto st = signals::parquet_read_signal_data_like_stream(parquet_reader, data,
			                                                            point_offset, point_count);
			    if (!st.ok())
				    throw std::runtime_error(st.ToString());
		    },
		    [&parquet_reader](signals::SignalData &data, int64_t off, int64_t cnt) {
			    auto st =
			        signals::parquet_read_signal_data_like_stream(parquet_reader, data, off, cnt);
			    if (!st.ok())
				    throw std::runtime_error(st.ToString());
		    },
		    reference, chunk_points,
		    [&parquet_path]() {
			    return std::filesystem::file_size(parquet_path);
		    },
		    original_size);

		signals::parquet_close(parquet_reader);

		all_ok = all_ok && ok;
	}

	if (run_duckdb) {
		std::filesystem::path duckdb_path = source_path.filename();
		duckdb_path.replace_extension(".duckdb");
		if (std::filesystem::exists(duckdb_path))
			std::filesystem::remove(duckdb_path);

		try {
			ok = run_format(
			    "DuckDB",
			    [&source_path, &duckdb_path, &point_offset, &point_count, &chunk_points]() {
				    signals::duckdb_write_signal_data_like_stream(
				        source_path, duckdb_path, point_offset, point_count, chunk_points);
			    },
			    [&duckdb_path, &point_offset, &point_count](signals::SignalData &data) {
				    signals::duckdb_read_signal_data_like_stream(duckdb_path, data, point_offset,
				                                                 point_count);
			    },
			    [&duckdb_path](signals::SignalData &data, int64_t off, int64_t cnt) {
				    signals::duckdb_read_signal_data_like_stream(duckdb_path, data, off, cnt);
			    },
			    reference, chunk_points,
			    [&duckdb_path]() {
				    return std::filesystem::file_size(duckdb_path);
			    },
			    original_size);

			all_ok = all_ok && ok;
		} catch (const std::exception &e) {
			std::printf("Ошибка DuckDB: %s\n\n", e.what());
			all_ok = false;
		}
	}
	return all_ok ? 0 : 1;
}