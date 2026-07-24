#include "../common/test_utils.hpp"
#include "hdf5_converter.hpp"

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
			std::fprintf(stderr, "Ошибка чтения offset\n");
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
	if (argc > arg_idx && (!signals::try_parse_int64(argv[arg_idx], chunk_points))) {
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
		std::filesystem::path hdf5_path = std::filesystem::path(source_path).filename();
		hdf5_path.replace_extension(".h5");

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

		if (std::filesystem::exists(hdf5_path))
			std::filesystem::remove(hdf5_path);

		std::printf("Запись в HDF5...\n");
		const double t_hdf5_write =
		    signals::lead_time([&bin_path, &hdf5_path, &point_offset, &point_count]() {
			    signals::hdf5_write_signals_data(bin_path, hdf5_path, point_offset, point_count);
		    });

		const size_t hdf5_file_size = std::filesystem::file_size(hdf5_path);

		signals::SignalData loaded_hdf5_data;
		const double t_hdf5_read =
		    signals::lead_time([&hdf5_path, &loaded_hdf5_data, &chunk_points]() {
			    signals::hdf5_read_signal_data_like_stream(hdf5_path, loaded_hdf5_data, 0, -1,
			                                               chunk_points);
		    });

		signals::print_conversion_results("HDF5", t_hdf5_write, hdf5_file_size, t_hdf5_read,
		                                  loaded_hdf5_data, original_total_size);

		auto read_signal = [&hdf5_path, &chunk_points](const std::filesystem::path &,
		                                               signals::SignalData &data, int64_t offset,
		                                               int64_t count) {
			signals::hdf5_read_signal_data_like_stream(hdf5_path, data, offset, count,
			                                           chunk_points);
		};

		signals::test_serial_chunk_reading_val(read_signal, hdf5_path, original_data, chunk_points);
		signals::test_random_chunk_reading_val(read_signal, hdf5_path, original_data, chunk_points);

		if (signals::compare_signal_data(original_data, loaded_hdf5_data, "BIN vs HDF5")) {
			std::printf("Данные совпадают полностью!\n\n");
			signals::print_first_rows(original_data);
			return 0;
		}

		std::fprintf(stderr, "Данные не совпадают!\n\n");
		signals::print_first_rows(original_data);
		std::filesystem::remove(hdf5_path);
		return 1;
	} catch (const std::exception &e) {
		std::fprintf(stderr, "Ошибка: %s\n", e.what());
		return 1;
	}
}