#include "signal_common.hpp"

#include <algorithm>
#include <stdexcept>

namespace signals {
	int64_t normalize_range(int64_t total_points, int64_t point_off, int64_t point_cnt) {
		const int64_t safe_total_points = std::max<int64_t>(0, total_points);
		if (point_off >= safe_total_points) {
			return 0;
		}

		const int64_t available = safe_total_points - point_off;
		return (point_cnt < 0) ? available : std::min(point_cnt, available);
	}

	int64_t make_header_for_range(const SignalHeader &header, int64_t total_points,
	                              int64_t point_off, int64_t point_cnt,
	                              SignalHeader &range_header) {
		const int64_t points_in_range = normalize_range(total_points, point_off, point_cnt);
		range_header = header;
		range_header.ifirst_point = header.ifirst_point + point_off;
		range_header.ilast_point = (points_in_range > 0)
		                               ? (range_header.ifirst_point + points_in_range - 1)
		                               : (range_header.ifirst_point - 1);
		return points_in_range;
	}

	int64_t prepare_signal_data_window(const SignalHeader &metadata, int64_t total_points,
	                                   int64_t point_off, int64_t point_cnt, SignalData &data,
	                                   bool zero_initialize_storage) {
		SignalHeader ranged_header;
		const int64_t points_in_range =
		    make_header_for_range(metadata, total_points, point_off, point_cnt, ranged_header);

		data = SignalData{};
		static_cast<SignalHeader &>(data) = ranged_header;
		data.num_points = points_in_range;

		if (points_in_range <= 0 || data.num_channels <= 0) {
			return points_in_range;
		}

		const size_t storage_size =
		    static_cast<size_t>(data.num_channels) * static_cast<size_t>(data.num_points);
		if (zero_initialize_storage) {
			data.storage.assign(storage_size, 0);
		} else {
			data.storage.resize(storage_size);
		}

		return points_in_range;
	}

	std::string serialize_to_json(
	    const std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>>
	        &dict) {
		std::stringstream json_stream;
		json_stream << "{";
		bool first = true;
		for (const auto &[key, val] : dict) {
			if (!first)
				json_stream << ",";
			json_stream << "\"" << key << "\":";

			// S6189 - использование std::visit для безопасного доступа к std::variant
			std::visit(
			    [&json_stream](const auto &arg) {
				    using T = std::decay_t<decltype(arg)>;
				    if constexpr (std::is_same_v<T, std::string>) {
					    json_stream << "\"" << arg << "\"";
				    } else {
					    json_stream << arg;
				    }
			    },
			    val);
			first = false;
		}
		json_stream << "}";
		return json_stream.str();
	}

	// S6009 - для передачи строки следует использовать std::string_view, чтобы избежать лишних
	// копирований
	std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>>
	parse_json(std::string_view json) {
		std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>> result;
		auto content = std::string(json.substr(1, json.size() - 2));
		std::stringstream segment_stream(content);
		std::string segment;

		while (std::getline(segment_stream, segment, ',')) {
			size_t colon_pos = segment.find(':');
			std::string key = segment.substr(0, colon_pos);
			std::string val = segment.substr(colon_pos + 1);

			key = key.substr(1, key.size() - 2);

			if (val[0] == '\"') {
				result[key] = val.substr(1, val.size() - 2);
			} else if (val.find('.') != std::string::npos) {
				result[key] = std::stod(val);
			} else {
				result[key] = std::stoll(val);
			}
		}
		return result;
	}
} // namespace signals