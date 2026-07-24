#include "../common/test_utils.hpp"
#include "../hdf5/hdf5_converter.hpp"

#include <cstdio>
#include <filesystem>

int main() {
	const auto base_path = signals::test_signal_base_path("hdf5");
	signals::create_test_signal_files(base_path);

	auto hdf5_path = base_path;
	hdf5_path.replace_extension(".h5");
	if (std::filesystem::exists(hdf5_path))
		std::filesystem::remove(hdf5_path);

	signals::hdf5_write_signals_data(base_path, hdf5_path, 0, -1);

	signals::SignalData full_data;
	signals::hdf5_read_signal_data_like_stream(hdf5_path, full_data, 0, -1, 16);
	if (!signals::validate_test_signal_data(full_data, 0, signals::TEST_SIGNAL_POINTS))
		return 1;

	signals::SignalData range_data;
	signals::hdf5_read_signal_data_like_stream(hdf5_path, range_data, 17, 23, 16);
	if (!signals::validate_test_signal_data(range_data, 17, 23))
		return 1;

	std::printf("HDF5 self-test passed\n");
	return 0;
}