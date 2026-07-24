#include "common/test_utils.hpp"
#include "hdf5/hdf5_converter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int64_t VERIFY_SAMPLE_POINTS = 10000;
constexpr int64_t VERIFY_CHUNK_POINTS = 100000;

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::fprintf(stderr, "Использование: %s <путь_к_папке> [количество_файлов]\n", argv[0]);
		return 1;
	}

	std::filesystem::path input_dir = argv[1];
	size_t max_files = SIZE_MAX;
	if (argc >= 3) {
		try {
			max_files = std::stoull(argv[2]);
			if (max_files == 0)
				throw std::invalid_argument("must be > 0");
		} catch (...) {
			std::fprintf(stderr, "Ошибка парсинга количества файлов: %s\n", argv[2]);
			return 1;
		}
	}

	if (!std::filesystem::exists(input_dir) || !std::filesystem::is_directory(input_dir)) {
		std::fprintf(stderr, "Ошибка: папка не существует или не является директорией: %s\n",
		             input_dir.string().c_str());
		return 1;
	}

	std::filesystem::path output_dir = input_dir / "hdf";
	std::filesystem::create_directories(output_dir);

	std::vector<std::filesystem::path> header_files;
	for (const auto &entry : std::filesystem::directory_iterator(input_dir))
		if (entry.is_regular_file() && entry.path().extension() == ".hdr")
			header_files.push_back(entry.path());
	std::sort(header_files.begin(), header_files.end());
	if (header_files.size() > max_files)
		header_files.resize(max_files);

	if (header_files.empty()) {
		std::printf("HDR файлы не найдены: %s\n", input_dir.string().c_str());
		return 0;
	}

	std::printf("Найдено HDR файлов: %zu\n", header_files.size());
	std::printf("Папка вывода: %s\n", output_dir.string().c_str());

	int success_count = 0;
	int error_count = 0;
	for (size_t i = 0; i < header_files.size(); i++) {
		const auto &header_path = header_files[i];
		try {
			std::filesystem::path base_path = header_path;
			base_path.replace_extension("");
			std::filesystem::path bin_path = base_path;
			bin_path.replace_extension(".bin");
			std::filesystem::path hdf5_path = output_dir / (base_path.filename().string() + ".h5");
			if (!std::filesystem::exists(header_path) || !std::filesystem::exists(bin_path)) {
				std::fprintf(stderr, "[%zu/%zu] Пропуск %s: HDR или BIN файл не найден\n", i + 1,
				             header_files.size(), base_path.filename().string().c_str());
				error_count++;
				continue;
			}
			if (std::filesystem::exists(hdf5_path))
				std::filesystem::remove(hdf5_path);
			std::printf("\n[%zu/%zu] Обработка: %s\n", i + 1, header_files.size(),
			            base_path.filename().string().c_str());
			const double t_hdf5_write = signals::lead_time([&bin_path, &hdf5_path]() {
				signals::hdf5_write_signals_data(bin_path, hdf5_path, 0, -1);
			});
			const size_t hdf5_file_size = std::filesystem::file_size(hdf5_path);
			std::printf("Размер HDF5 файла: %.2f MB, время записи: %.3f сек\n",
			            static_cast<double>(hdf5_file_size) / (1024.0 * 1024.0), t_hdf5_write);

			signals::BinReader reader;
			signals::bin_open(header_path, reader);
			const int64_t total_points = reader.total_points;
			signals::bin_close(reader);
			const int64_t window_points = std::min<int64_t>(VERIFY_SAMPLE_POINTS, total_points);
			auto compare_window = [&](int64_t offset, const char *label) {
				signals::SignalData expected;
				signals::SignalData actual;
				signals::bin_read(header_path, offset, window_points, expected);
				signals::hdf5_read_signal_data_like_stream(hdf5_path, actual, offset, window_points,
				                                           VERIFY_CHUNK_POINTS);
				return signals::compare_signal_data(expected, actual, label);
			};
			bool ok = true;
			if (!compare_window(0, "BIN vs HDF5 [head]")) {
				std::fprintf(stderr, "[%zu/%zu] Ошибка: обработка не удалась\n", i + 1,
				             header_files.size());
				ok = false;
			}
			if (total_points > window_points) {
				int64_t tail_offset = total_points - window_points;
				if (!compare_window(tail_offset, "BIN vs HDF5 [tail]")) {
					std::fprintf(stderr, "[%zu/%zu] Ошибка проверка не удалась\n", i + 1,
					             header_files.size());
					ok = false;
				}
			}
			if (ok)
				success_count++;
			else
				error_count++;
		} catch (const std::exception &e) {
			std::fprintf(stderr, "[%zu/%zu] Ошибка при обработке %s: %s\n", i + 1,
			             header_files.size(), header_path.filename().string().c_str(), e.what());
			error_count++;
		}
	}
	std::printf("Готово. Успешно: %d, Ошибок: %d\n", success_count, error_count);
	return (error_count == 0) ? 0 : 1;
}