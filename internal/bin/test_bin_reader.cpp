#include "bin_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

static constexpr int64_t PREVIEW_POINTS = 10;

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::fprintf(stderr, "Необходимо передать файл, запуск: %s <file> [offset] [count]\n",
		             argv[0]);
		std::fprintf(stderr, "  offset - индекс начала чтения (по умолчанию: 0)\n");
		std::fprintf(stderr, "  count  - количество точек для чтения     (по умолчанию: все)\n");
		return 1;
	}

	int64_t point_offset = 0;
	int64_t point_count = -1;
	try {
		if (argc >= 3)
			point_offset = std::stoll(argv[2]);

		if (argc >= 4)
			point_count = std::stoll(argv[3]);
	} catch (...) {
		std::fprintf(stderr, "Ошибка чтения offset/count\n");
		return 1;
	}

	signals::SignalData data;
	const auto t0 = std::chrono::steady_clock::now();
	try {
		signals::bin_read(argv[1], point_offset, point_count, data);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "Ошибка: %s\n", e.what());
		return 1;
	}

	const double t_read =
	    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("Чтение — %.3f сек\n", t_read);

	std::printf("BIN: %s\n", argv[1]);
	std::printf("Каналов:    %d\n", data.num_channels);
	std::printf("Частота: %.1f Hz\n", data.frequency);
	std::printf("Время начала:  %s\n", data.time_start.c_str());
	std::printf("Точек:     %lld\n", static_cast<long long>(data.num_points));

	std::printf("\n Количество точек %lld:\n", static_cast<long long>(PREVIEW_POINTS));
	std::printf("%-6s", "Row");
	for (int ch = 0; ch < data.num_channels; ch++) {
		const std::string channel_name = (static_cast<size_t>(ch) < data.channel_names.size() &&
		                                  !data.channel_names[static_cast<size_t>(ch)].empty())
		                                     ? data.channel_names[static_cast<size_t>(ch)]
		                                     : ("Ch" + std::to_string(ch));
		std::printf("%-14s", channel_name.c_str());
	}
	std::printf("\n");

	for (int64_t row = 0; row < PREVIEW_POINTS; row++) {
		std::printf("%-10lld", row + 1);
		for (int ch = 0; ch < data.num_channels; ch++) {
			std::printf("%-14.4f", data.value(ch, row));
		}
		std::printf("\n");
	}

	std::printf("=================================================\n");

	for (int64_t row = data.num_points - PREVIEW_POINTS; row < data.num_points; row++) {
		std::printf("%-10lld", row + 1);
		for (int ch = 0; ch < data.num_channels; ch++) {
			std::printf("%-14.4f", data.value(ch, row));
		}
		std::printf("\n");
	}

	return 0;
}