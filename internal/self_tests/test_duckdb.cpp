#include "../common/test_utils.hpp"
#include "../duckdb/duckdb_converter.hpp"

#include <cstdio>
#include <filesystem>

int main() {
	const auto base_path = signals::test_signal_base_path("duckdb");
	signals::create_test_signal_files(base_path);

	auto duckdb_path = base_path;
	duckdb_path.replace_extension(".duckdb");
	if (std::filesystem::exists(duckdb_path))
		std::filesystem::remove(duckdb_path);

	signals::duckdb_write_signal_data_like_stream(base_path, duckdb_path, 0, -1);

	signals::SignalData full_data;
	signals::duckdb_read_signal_data_like_stream(duckdb_path, full_data, 0, -1);
	if (!signals::validate_test_signal_data(full_data, 0, signals::TEST_SIGNAL_POINTS))
		return 1;

	signals::SignalData range_data;
	signals::duckdb_read_signal_data_like_stream(duckdb_path, range_data, 17, 23);
	if (!signals::validate_test_signal_data(range_data, 17, 23))
		return 1;

	std::printf("DuckDB self-test passed\n");
	return 0;
}