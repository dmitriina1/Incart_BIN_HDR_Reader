#include "../common/test_utils.hpp"
#include "duckdb_converter.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::fprintf(stderr,
		             "Использование: %s <file.bin|file.hdr> [offset] [count] [chunk_points]\n",
		             argv[0]);
		return 1;
	}

	const std::filesystem::path source_path = argv[1];
	int64_t point_offset = 0;
	int64_t point_count = -1;
	int64_t chunk_points = 100000;

	int arg_idx = 2;
	if (argc > arg_idx) {
		if (!signals::try_parse_int64(argv[arg_idx], point_offset)) {
			std::fprintf(stderr, "Ошибка чтения второго аргумента\n");
			return 1;
		}
		arg_idx++;
	}
	if (argc > arg_idx) {
		if (!signals::try_parse_int64(argv[arg_idx], point_count)) {
			std::fprintf(stderr, "Ошибка чтения count\n");
			return 1;
		}
		arg_idx++;
	}
	if (argc > arg_idx && !signals::try_parse_int64(argv[arg_idx], chunk_points)) {
		std::fprintf(stderr, "Ошибка чтения chunk_points\n");
		return 1;
	}

	try {
		auto base_path = source_path;
		base_path.replace_extension("");
		auto bin_path = base_path;
		bin_path.replace_extension(".bin");
		auto header_path = base_path;
		header_path.replace_extension(".hdr");

		std::filesystem::path duckdb_path = std::filesystem::path(source_path).filename();
		duckdb_path.replace_extension(".duckdb");

		if (!std::filesystem::exists(header_path) || !std::filesystem::exists(bin_path)) {
			std::fprintf(stderr, "HDR или BIN файл не найден: %s\n", header_path.string().c_str());
			return 1;
		}

		const size_t original_total_size =
		    std::filesystem::file_size(bin_path) + std::filesystem::file_size(header_path);
		signals::SignalData original_data =
		    signals::load_reference(header_path, point_offset, point_count);

		signals::SignalData ref_data;
		const double t_bin =
		    signals::lead_time([&header_path, &point_offset, &point_count, &ref_data]() {
			    signals::bin_read(header_path, point_offset, point_count, ref_data);
		    });

		signals::print_file_info(header_path, original_data, t_bin);

		if (std::filesystem::exists(duckdb_path))
			std::filesystem::remove(duckdb_path);

		std::printf("Запись в DuckDB...\n");
		const double t_duckdb_write = signals::lead_time(
		    [&bin_path, &duckdb_path, &point_offset, &point_count, &chunk_points]() {
			    signals::duckdb_write_signal_data_like_stream(bin_path, duckdb_path, point_offset,
			                                                  point_count, chunk_points);
		    });

		const size_t duckdb_file_size = std::filesystem::file_size(duckdb_path);

		signals::SignalData loaded_duckdb_data;
		const double t_duckdb_read = signals::lead_time([&duckdb_path, &loaded_duckdb_data]() {
			signals::duckdb_read_signal_data_like_stream(duckdb_path, loaded_duckdb_data, 0, -1);
		});

		signals::print_conversion_results("DuckDB", t_duckdb_write, duckdb_file_size, t_duckdb_read,
		                                  loaded_duckdb_data, original_total_size);

		auto read_signal = [](const std::filesystem::path &path, signals::SignalData &data,
		                      int64_t offset, int64_t count) {
			signals::duckdb_read_signal_data_like_stream(path, data, offset, count);
		};

		signals::test_serial_chunk_reading_val(read_signal, duckdb_path, original_data,
		                                       chunk_points);
		signals::test_random_chunk_reading_val(read_signal, duckdb_path, original_data,
		                                       chunk_points);

		if (signals::compare_signal_data(original_data, loaded_duckdb_data, "BIN vs DuckDB")) {
			std::printf("Данные совпадают полностью!\n\n");
			signals::print_first_rows(original_data);
			return 0;
		}

		std::fprintf(stderr, "Данные не совпадают!\n\n");
		signals::print_first_rows(original_data);
		return 1;
	} catch (const std::exception &e) {
		std::fprintf(stderr, "Ошибка: %s\n", e.what());
		return 1;
	}
}