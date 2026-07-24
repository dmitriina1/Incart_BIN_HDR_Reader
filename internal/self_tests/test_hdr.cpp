#include "../common/test_utils.hpp"
#include "../hdr/hdr_reader.hpp"

#include <cstdio>

int main() {
	const auto base_path = signals::test_signal_base_path("hdr");
	signals::create_test_signal_files(base_path);

	signals::SignalHeader header;
	signals::hdr_read(base_path, header);

	if (!signals::validate_test_signal_header(header))
		return 1;

	std::printf("HDR self-test passed\n");
	return 0;
}