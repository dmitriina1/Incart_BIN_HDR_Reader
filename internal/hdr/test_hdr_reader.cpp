#include "hdr_reader.hpp"

#include <chrono>
#include <cstdio>

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::fprintf(stderr, "Необходимо передать файл, запуск в формате: %s <file.hdr>\n",
		             argv[0]);
		return 1;
	}

	signals::SignalHeader header;
	const auto t0 = std::chrono::steady_clock::now();
	signals::hdr_read(argv[1], header);
	const double t_read =
	    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

	std::printf("Чтение — %.3f сек\n", t_read);

	std::printf("HDR: %s\n", argv[1]);
	std::printf("Channels:     %d\n", header.num_channels);
	std::printf("Point rate:  %.1f Hz\n", header.frequency);
	std::printf("Default LSB:  %.6f\n", header.lsb_default);
	std::printf("IStart, IEnd:      %lld - %lld\n", static_cast<long long>(header.ifirst_point),
	            static_cast<long long>(header.ilast_point));
	std::printf("Time start:   %s\n", header.time_start.c_str());

	std::printf("\nChannel  LSB          Unit   Name\n");
	for (int i = 0; i < header.num_channels; i++) {
		std::printf("  %3d    %11.6f  %-5s  %s\n", i, header.lsbs[i], header.units[i].c_str(),
		            header.channel_names[i].c_str());
	}

	return 0;
}