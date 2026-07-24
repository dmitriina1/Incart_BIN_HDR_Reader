#include "test_utils.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

const int64_t TEST_UTILS_PREVIEW_POINTS = 10;

namespace signals {
	static int32_t generate_test_signal_raw_value(int channel, int64_t point);
	static int64_t normalize_chunk_points(int64_t chunk_points, int64_t total_points);
	static std::mt19937 &chunk_rng();
	static std::vector<int64_t> make_chunk_order(int64_t num_chunks, bool random_order);
	static bool validate_chunk_data(const SignalData &reference, const SignalData &chunk_data,
	                                int64_t chunk_offset, int64_t chunk_count, int64_t chunk_index,
	                                size_t order_index);
	static double benchmark_chunk_reading_impl(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference, int64_t chunk_points,
	    bool random_order);
	static bool validate_chunk_reading_impl(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t point_off,
	                             int64_t point_cnt)> &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference, int64_t chunk_points,
	    bool random_order);

	double lead_time(const std::function<void()> &fn) {
		const auto t0 = std::chrono::steady_clock::now();
		fn();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}

	double test_serial_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points) {
		return benchmark_chunk_reading_impl(read_signal, source_path, reference, chunk_points,
		                                    false);
	}

	double test_random_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points) {
		return benchmark_chunk_reading_impl(read_signal, source_path, reference, chunk_points,
		                                    true);
	}

	bool validate_serial_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points) {
		return validate_chunk_reading_impl(read_signal, source_path, reference, chunk_points,
		                                   false);
	}

	bool validate_random_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points) {
		return validate_chunk_reading_impl(read_signal, source_path, reference, chunk_points, true);
	}

	void test_serial_chunk_reading_val(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &original_data,
	    int64_t chunk_points) {
		const double total_time =
		    test_serial_chunk_reading(read_signal, source_path, original_data, chunk_points);

		if (!validate_serial_chunk_reading(read_signal, source_path, original_data, chunk_points))
			return;

		if (original_data.num_points <= 0 || original_data.num_channels <= 0) {
			std::printf("Последовательное чтение: 0 чанков, 0.000 сек (0.0 отсчетов/сек)\n");
			return;
		}

		const int64_t safe_chunk_points =
		    normalize_chunk_points(chunk_points, original_data.num_points);
		const int64_t num_chunks =
		    (original_data.num_points + safe_chunk_points - 1) / safe_chunk_points;
		const double samples_per_second =
		    (total_time > 0.0) ? (static_cast<double>(original_data.num_points) *
		                          static_cast<double>(original_data.num_channels) / total_time)
		                       : 0.0;

		std::printf("Последовательное чтение: %lld чанков, %.3f сек (%.1f отсчетов/сек)\n",
		            num_chunks, total_time, samples_per_second);
	}

	void test_random_chunk_reading_val(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &original_data,
	    int64_t chunk_points) {
		const double total_time =
		    test_random_chunk_reading(read_signal, source_path, original_data, chunk_points);

		if (!validate_random_chunk_reading(read_signal, source_path, original_data, chunk_points))
			return;

		if (original_data.num_points <= 0 || original_data.num_channels <= 0) {
			std::printf("Случайный порядок: 0 чанков, 0.000 сек (0.0 отсчетов/сек)\n");
			return;
		}

		const int64_t safe_chunk_points =
		    normalize_chunk_points(chunk_points, original_data.num_points);
		const int64_t num_chunks =
		    (original_data.num_points + safe_chunk_points - 1) / safe_chunk_points;
		const double samples_per_second =
		    (total_time > 0.0) ? (static_cast<double>(original_data.num_points) *
		                          static_cast<double>(original_data.num_channels) / total_time)
		                       : 0.0;

		std::printf("Случайный порядок: %lld чанков, %.3f сек (%.1f отсчетов/сек)\n", num_chunks,
		            total_time, samples_per_second);
	}

	void print_first_rows(const SignalData &data, int64_t preview_points) {
		if (data.num_points == 0 || data.num_channels == 0) {
			std::printf("Нет данных для отображения\n");
			return;
		}

		const int64_t pp = std::min<int64_t>(preview_points, data.num_points);

		std::printf("\n%-6s", "Row");
		for (int ch = 0; ch < data.num_channels; ch++) {
			const std::string channel_name = (static_cast<size_t>(ch) < data.channel_names.size() &&
			                                  !data.channel_names[static_cast<size_t>(ch)].empty())
			                                     ? data.channel_names[static_cast<size_t>(ch)]
			                                     : std::format("Ch {}", ch);
			std::printf("%-14s", channel_name.c_str());
		}
		std::printf("\n");

		for (int64_t row = 0; row < pp; row++) {
			std::printf("%-10lld", static_cast<long long>(row + 1));
			for (int ch = 0; ch < data.num_channels; ch++)
				std::printf("%-14.4f", data.value(ch, row));
			std::printf("\n");
		}

		if (data.num_points > pp * 2)
			std::printf("...\n");

		const int64_t start_last = std::max<int64_t>(data.num_points - pp, pp);
		for (int64_t row = start_last; row < data.num_points; row++) {
			std::printf("%-10lld", static_cast<long long>(row + 1));
			for (int ch = 0; ch < data.num_channels; ch++)
				std::printf("%-14.4f", data.value(ch, row));
			std::printf("\n");
		}
	}

	bool compare_signal_data(const SignalData &expected, const SignalData &actual,
	                         const char *label) {
		if (expected.num_points != actual.num_points ||
		    expected.num_channels != actual.num_channels) {
			std::fprintf(stderr, "Ошибка (%s): точек или каналов не совпадает: %lld vs %lld\n",
			             label, static_cast<long long>(expected.num_points),
			             static_cast<long long>(actual.num_points));
			return false;
		}

		if (expected.ifirst_point != actual.ifirst_point ||
		    expected.ilast_point != actual.ilast_point) {
			std::fprintf(stderr, "Ошибка (%s): диапазоны не совпадают: %lld..%lld vs %lld..%lld\n",
			             label, static_cast<long long>(expected.ifirst_point),
			             static_cast<long long>(expected.ilast_point),
			             static_cast<long long>(actual.ifirst_point),
			             static_cast<long long>(actual.ilast_point));
			return false;
		}

		for (int ch = 0; ch < expected.num_channels; ch++) {
			for (int64_t p = 0; p < expected.num_points; p++) {
				if (expected.raw_channel(ch)[p] != actual.raw_channel(ch)[p]) {
					std::fprintf(stderr,
					             "Ошибка (%s): несовпадение на канале %d, точка %lld: %f vs %f\n",
					             label, ch, static_cast<long long>(p), expected.value(ch, p),
					             actual.value(ch, p));
					return false;
				}
			}
		}
		return true;
	}

	SignalData load_reference(const std::filesystem::path &source_path, int64_t point_offset,
	                          int64_t point_count) {
		SignalData ref;
		bin_read(source_path, point_offset, point_count, ref);
		return ref;
	}

	void print_file_info(const std::filesystem::path &path, const SignalData &data,
	                     double read_time) {
		auto base = std::filesystem::path(path).replace_extension("");
		auto header_path = base;

		header_path.replace_extension(".hdr");
		const size_t header_size = std::filesystem::file_size(header_path);

		auto bin_path = base.replace_extension(".bin");
		const size_t bin_size = std::filesystem::file_size(bin_path);

		std::printf("Исходные данные (BIN):\n");
		std::printf("Каналов: %d\n", data.num_channels);
		std::printf("Точек: %lld\n", static_cast<long long>(data.num_points));
		std::printf("Диапазон: %lld..%lld\n", static_cast<long long>(data.ifirst_point),
		            static_cast<long long>(data.ilast_point));
		std::printf("Частота: %.1f Гц\n", data.frequency);
		std::printf("Размер файлов: BIN=%.1f MB, HDR=%.1f KB\n",
		            static_cast<double>(bin_size) / (1024.0 * 1024.0),
		            static_cast<double>(header_size) / 1024.0);
		std::printf("Время чтения BIN: %.3f сек\n", read_time);
	}

	void print_conversion_results(const std::string &format_name, double write_time,
	                              size_t output_size_bytes, double read_time,
	                              const SignalData &data, size_t original_total_size) {
		std::printf("Файл %s написан\n", format_name.c_str());
		std::printf("Размер: %.1f MB\n",
		            static_cast<double>(output_size_bytes) / (1024.0 * 1024.0));
		std::printf("Время записи: %.3f сек\n", write_time);
		if (original_total_size > 0)
			std::printf("  Сжатие к BIN+HDR: %.1f%%\n",
			            100.0 * (1.0 - (static_cast<double>(output_size_bytes) /
			                            static_cast<double>(original_total_size))));
		std::printf("\n");

		std::printf("Чтение из %s...\n", format_name.c_str());
		std::printf("Каналов: %d\n", data.num_channels);
		std::printf("Точек: %lld\n", static_cast<long long>(data.num_points));
		std::printf("Диапазон: %lld..%lld\n", static_cast<long long>(data.ifirst_point),
		            static_cast<long long>(data.ilast_point));
		std::printf("Частота: %.1f Гц\n", data.frequency);
		std::printf("Время чтения: %.3f сек\n", read_time);
		std::printf("\n");
	}

	bool try_parse_int64(const char *text, int64_t &value) {
		if (!text || *text == '\0')
			return false;

		const char *end = text + strlen(text);
		auto [ptr, ec] = std::from_chars(text, end, value);

		return ec == std::errc() && ptr == end;
	}

	std::filesystem::path test_signal_base_path(const char *test_name) {
		const auto dir = std::filesystem::current_path() / "test_data";
		std::filesystem::create_directories(dir);
		return dir / test_name;
	}

	static int32_t generate_test_signal_raw_value(int channel, int64_t point) {
		const auto value = static_cast<int32_t>((channel + 1) * 1000 + point * 7);
		return ((channel + point) % 2 == 0) ? value : -value;
	}

	void create_test_signal_files(const std::filesystem::path &base_path) {
		std::filesystem::create_directories(base_path.parent_path());

		auto header_path = base_path;
		header_path.replace_extension(".hdr");
		auto bin_path = base_path;
		bin_path.replace_extension(".bin");

		{
			std::ofstream header_file(header_path, std::ios::binary | std::ios::trunc);
			if (!header_file)
				throw std::runtime_error(std::format("Не получилось создать тестовые данные HDR {}",
				                                     header_path.string()));

			header_file << TEST_SIGNAL_CHANNELS << '\t' << TEST_SIGNAL_FREQUENCY << '\t'
			            << TEST_SIGNAL_LSB << '\n';
			header_file << "0\t" << TEST_SIGNAL_POINTS << "\t2026-05-21T12:00:00\n";
			header_file << "LR\tFR\tC1R\tC2L\tC3F\tC4R\tC5L\tC6F\n";
			header_file
			    << "2.29885\t2.29885\t2.29885\t2.29885\t2.29885\t2.29885\t2.29885\t2.29885\n";
			header_file << "uV\tuV\tuV\tuV\tuV\tuV\tuV\tuV\n";
			header_file << "tohead\n";
			header_file << "s:source:self-test\n";
			header_file << "l:points:" << TEST_SIGNAL_POINTS << '\n';
		}

		{
			std::ofstream bin(bin_path, std::ios::binary | std::ios::trunc);
			if (!bin)
				throw std::runtime_error(
				    std::format("Не удалось открыть BIN файл: {}", bin_path.string()));

			for (int64_t point = 0; point < TEST_SIGNAL_POINTS; point++) {
				for (int channel = 0; channel < TEST_SIGNAL_CHANNELS; channel++) {
					const int32_t value = generate_test_signal_raw_value(channel, point);
					bin.write(reinterpret_cast<const char *>(&value), sizeof(value)); // NOSONAR
				}
			}
		}
	}

	bool validate_test_signal_header(const SignalHeader &header) {
		if (header.num_channels != TEST_SIGNAL_CHANNELS) {
			std::fprintf(stderr, "Ожидалось %d каналов, а сейчас %d\n", TEST_SIGNAL_CHANNELS,
			             header.num_channels);
			return false;
		}

		if (std::fabs(header.frequency - TEST_SIGNAL_FREQUENCY) > 1e-9 ||
		    std::fabs(header.lsb_default - TEST_SIGNAL_LSB) > 1e-9) {
			std::fprintf(stderr, "Не совпадает frequency/default LSB\n");
			return false;
		}

		if (header.channel_names.size() != TEST_SIGNAL_CHANNELS ||
		    header.channel_names[0] != "LR" || header.channel_names[7] != "C6F") {
			std::fprintf(stderr, "Не совпадают названия каналов\n");
			return false;
		}

		if (header.lsbs.size() != TEST_SIGNAL_CHANNELS ||
		    header.units.size() != TEST_SIGNAL_CHANNELS) {
			std::fprintf(stderr, "Не совпадает количество LSB/unit\n");
			return false;
		}

		return true;
	}

	static bool validate_signal_range(const SignalHeader &header, int64_t first_point,
	                                  int64_t point_count) {
		if (const int64_t expected_last_point = first_point + point_count - 1;
		    header.ifirst_point != first_point || header.ilast_point != expected_last_point) {
			std::fprintf(stderr, "Диапазон точек не совпал\n");
			return false;
		}

		return true;
	}

	bool validate_test_signal_data(const SignalData &data, int64_t first_point,
	                               int64_t point_count) {
		if (!validate_test_signal_header(data)) {
			return false;
		}

		if (data.num_points != point_count) {
			std::fprintf(stderr, "Ожидалось %lld точек, а сейчас %lld\n",
			             static_cast<long long>(point_count),
			             static_cast<long long>(data.num_points));
			return false;
		}

		if (!validate_signal_range(data, first_point, point_count)) {
			return false;
		}

		for (int channel = 0; channel < TEST_SIGNAL_CHANNELS; channel++) {
			for (int64_t point = 0; point < point_count; point++) {
				const int32_t expected =
				    generate_test_signal_raw_value(channel, first_point + point);
				const int32_t actual = data.raw_channel(channel)[point];

				if (actual != expected) {
					std::fprintf(
					    stderr, "Ошибка в канале %d точка %lld: ожидалось %d, а сейчас %d\n",
					    channel, static_cast<long long>(first_point + point), expected, actual);
					return false;
				}
			}
		}

		return true;
	}

	static int64_t normalize_chunk_points(int64_t chunk_points, int64_t total_points) {
		if (total_points <= 0)
			return 1;

		if (chunk_points <= 0)
			return total_points;

		return chunk_points;
	}

	static std::mt19937 &chunk_rng() {
		static thread_local std::mt19937 rng(std::random_device{}());
		return rng;
	}

	static std::vector<int64_t> make_chunk_order(int64_t num_chunks, bool random_order) {
		std::vector<int64_t> chunk_order(static_cast<size_t>(num_chunks));
		for (int64_t i = 0; i < num_chunks; i++)
			chunk_order[static_cast<size_t>(i)] = i;

		if (random_order)
			std::ranges::shuffle(chunk_order, chunk_rng());

		return chunk_order;
	}

	static bool validate_chunk_data(const SignalData &reference, const SignalData &chunk_data,
	                                int64_t chunk_offset, int64_t chunk_count, int64_t chunk_index,
	                                size_t order_index) {
		if (chunk_data.num_channels != reference.num_channels ||
		    chunk_data.num_points != chunk_count) {
			std::fprintf(
			    stderr,
			    "Ошибка в чанке %lld (порядок %zu, offset=%lld): ожидалось num_channels=%d и "
			    "num_points=%lld, а сейчас num_channels=%d и num_points=%lld\n",
			    chunk_index, order_index, chunk_offset, reference.num_channels, chunk_count,
			    chunk_data.num_channels, chunk_data.num_points);
			return false;
		}

		for (int ch = 0; ch < reference.num_channels; ch++) {
			for (int64_t point = 0; point < chunk_data.num_points; point++) {
				if (reference.raw_channel(ch)[chunk_offset + point] !=
				    chunk_data.raw_channel(ch)[point]) {
					std::fprintf(stderr,
					             "Ошибка в чанке %lld (порядок %zu, offset=%lld): канал %d, точка "
					             "%lld: ожидалось %d, а сейчас %d\n",
					             chunk_index, order_index, chunk_offset, ch, chunk_offset + point,
					             reference.raw_channel(ch)[chunk_offset + point],
					             chunk_data.raw_channel(ch)[point]);
					return false;
				}
			}
		}
		return true;
	}

	static double benchmark_chunk_reading_impl(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference, int64_t chunk_points,
	    bool random_order) {
		if (reference.num_points <= 0 || reference.num_channels <= 0)
			return 0.0;

		const int64_t safe_chunk_points =
		    normalize_chunk_points(chunk_points, reference.num_points);
		const int64_t num_chunks =
		    (reference.num_points + safe_chunk_points - 1) / safe_chunk_points;

		std::vector<int64_t> chunk_order;
		if (random_order)
			chunk_order = make_chunk_order(num_chunks, true);

		SignalData chunk_data;
		chunk_data.storage.reserve(static_cast<size_t>(reference.num_channels) *
		                           static_cast<size_t>(safe_chunk_points));

		const auto t0 = std::chrono::steady_clock::now();
		for (int64_t i = 0; i < num_chunks; i++) {
			const int64_t chunk_index = random_order ? chunk_order[static_cast<size_t>(i)] : i;
			const int64_t chunk_offset = chunk_index * safe_chunk_points;
			const int64_t chunk_count =
			    std::min(safe_chunk_points, reference.num_points - chunk_offset);
			read_signal(source_path, chunk_data, chunk_offset, chunk_count);
		}

		return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}

	static bool validate_chunk_reading_impl(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference, int64_t chunk_points,
	    bool random_order) {
		if (reference.num_points <= 0 || reference.num_channels <= 0)
			return true;

		const int64_t safe_chunk_points =
		    normalize_chunk_points(chunk_points, reference.num_points);
		const int64_t num_chunks =
		    (reference.num_points + safe_chunk_points - 1) / safe_chunk_points;

		std::vector<int64_t> chunk_order;
		if (random_order)
			chunk_order = make_chunk_order(num_chunks, true);

		SignalData chunk_data;
		chunk_data.storage.reserve(static_cast<size_t>(reference.num_channels) *
		                           static_cast<size_t>(safe_chunk_points));

		for (int64_t i = 0; i < num_chunks; i++) {
			const int64_t chunk_index = random_order ? chunk_order[static_cast<size_t>(i)] : i;
			const int64_t chunk_offset = chunk_index * safe_chunk_points;
			const int64_t chunk_count =
			    std::min(safe_chunk_points, reference.num_points - chunk_offset);

			read_signal(source_path, chunk_data, chunk_offset, chunk_count);

			if (!validate_chunk_data(reference, chunk_data, chunk_offset, chunk_count, chunk_index,
			                         static_cast<size_t>(i)))
				return false;
		}
		return true;
	}

} // namespace signals