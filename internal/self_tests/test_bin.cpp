#include "../bin/bin_reader.hpp"
#include "../common/test_utils.hpp"

#include <cstdio>

int main() {
	const auto base_path = signals::test_signal_base_path("bin");
	signals::create_test_signal_files(base_path);

	signals::SignalData full_data;
	signals::bin_read(base_path, 0, -1, full_data);
	if (!signals::validate_test_signal_data(full_data, 0, signals::TEST_SIGNAL_POINTS))
		return 1;

	signals::SignalData range_data;
	signals::bin_read(base_path, 17, 23, range_data);
	if (!signals::validate_test_signal_data(range_data, 17, 23))
		return 1;

	std::printf("BIN self-test passed\n");
	return 0;
}