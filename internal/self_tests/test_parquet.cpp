#include "../common/test_utils.hpp"
#include "../parquet/parquet_converter.hpp"

#include <cstdio>
#include <filesystem>
#include <stdexcept>

int main() {
	const auto base_path = signals::test_signal_base_path("parquet");
	signals::create_test_signal_files(base_path);

	auto parquet_path = base_path;
	parquet_path.replace_extension(".parquet");
	if (std::filesystem::exists(parquet_path))
		std::filesystem::remove(parquet_path);

	if (const auto write_status =
	        signals::parquet_write_signals_data(base_path, parquet_path, 0, -1, 16);
	    !write_status.ok())
		throw std::runtime_error(write_status.ToString());

	signals::SignalData full_data;
	signals::parquet_read_signal_data_like_stream(parquet_path, full_data, 0, -1);
	if (!signals::validate_test_signal_data(full_data, 0, signals::TEST_SIGNAL_POINTS))
		return 1;

	signals::SignalData range_data;
	signals::parquet_read_signal_data_like_stream(parquet_path, range_data, 17, 23);
	if (!signals::validate_test_signal_data(range_data, 17, 23))
		return 1;

	std::printf("Parquet self-test passed\n");
	return 0;
}