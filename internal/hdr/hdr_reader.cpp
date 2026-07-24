#include "hdr_reader.hpp"

#include <cerrno>
#include <iostream>
#include <system_error>

namespace signals {

	static void bom_skip(std::ifstream &file);
	static std::vector<std::string> split_line(const std::string &line);
	static std::vector<std::string> read_data_line(std::istream &in);
	static void parse_numchan_fs_lsb(std::ifstream &file, SignalHeader &header);
	static void parse_range_timestamp(std::ifstream &file, SignalHeader &header);
	static void parse_channel_names(std::ifstream &file, SignalHeader &header);
	static void parse_lsbs(std::ifstream &file, SignalHeader &header);
	static void parse_units(std::ifstream &file, SignalHeader &header);
	static void parse_optional_params(std::ifstream &file, SignalHeader &header);

	void hdr_read(const std::filesystem::path &bin_path, SignalHeader &header) {
		auto header_path = std::filesystem::path(bin_path).replace_extension(".hdr");

		std::ifstream header_file(header_path, std::ios::binary);
		if (!header_file) {
			throw std::system_error(errno, std::generic_category(),
			                        "Не получается открыть файл: " + header_path.string());
		}

		bom_skip(header_file);

		header = SignalHeader{};

		parse_numchan_fs_lsb(header_file, header);
		parse_range_timestamp(header_file, header);
		parse_channel_names(header_file, header);
		parse_lsbs(header_file, header);
		parse_units(header_file, header);
		parse_optional_params(header_file, header);
	}

	static void parse_optional_params(std::ifstream &file, SignalHeader &header) {
		std::string line;
		while (std::getline(file, line)) {
			if (line == "tohead")
				continue;

			size_t first_position = line.find(':');
			if (first_position == std::string::npos)
				continue;

			size_t second_position = line.find(':', first_position + 1);
			if (second_position == std::string::npos)
				continue;

			std::string type = line.substr(0, first_position);
			std::string name =
			    line.substr(first_position + 1, second_position - (first_position + 1));
			std::string value = line.substr(second_position + 1);

			if (type == "l") {
				header.optional_fields[name] = std::stoll(value);
			} else {
				header.optional_fields[name] = value;
			}
		}
	}

	static void parse_units(std::ifstream &file, SignalHeader &header) {
		auto header_params = read_data_line(file);
		if (header_params.size() < static_cast<size_t>(header.num_channels)) {
			throw std::runtime_error("Неверный формат: количество единиц измерения не "
			                         "совпадает с количеством каналов");
		}
		header.units.resize(header.num_channels);
		for (int i = 0; i < header.num_channels; i++) {
			header.units[i] = header_params[i];
		}
	}

	static void bom_skip(std::ifstream &file) {
		char bom[3] = {};
		if (!(file.read(bom, 3).gcount() == 3 && static_cast<unsigned char>(bom[0]) == 0xEF &&
		      static_cast<unsigned char>(bom[1]) == 0xBB &&
		      static_cast<unsigned char>(bom[2]) == 0xBF)) {
			file.seekg(0);
		}
	}

	static std::vector<std::string> split_line(const std::string &line) {
		std::istringstream strstream(line);
		std::vector<std::string> res;
		std::string tempstr;
		while (strstream >> tempstr) {
			res.push_back(std::move(tempstr));
		}
		return res;
	}

	static std::vector<std::string> read_data_line(std::istream &in) {
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			auto header_params = split_line(line);
			if (!header_params.empty()) {
				return header_params;
			}
		}
		return {};
	}

	static void parse_numchan_fs_lsb(std::ifstream &file, SignalHeader &header) {
		auto header_params = read_data_line(file);
		if (header_params.size() < 3) {
			throw std::runtime_error(
			    "Неверный формат: недостаточно параметров в первой строке (ожидаются "
			    "num_channels, frequency, lsb_default)");
		}
		try {
			header.num_channels = std::stoi(header_params[0]);
			header.frequency = std::stod(header_params[1]);
			header.lsb_default = std::stod(header_params[2]);
		} catch (const std::exception &) {
			throw std::runtime_error(
			    "Неверный формат: не удается распарсить num_channels, frequency или "
			    "lsb_default из первой строки заголовка");
		}
	}

	static void parse_range_timestamp(std::ifstream &file, SignalHeader &header) {
		auto header_params = read_data_line(file);
		if (header_params.size() < 3) {
			throw std::runtime_error(
			    "Неверный формат: недостаточно параметров во второй строке (ожидаются "
			    "ifirst_point, point_count, time_start)");
		}
		try {
			header.ifirst_point = std::stoll(header_params[0]);
			int64_t point_count = std::stoll(header_params[1]);
			header.ilast_point = header.ifirst_point + point_count - 1;
		} catch (const std::exception &) {
			throw std::runtime_error(
			    "Неверный формат: не удается распарсить ifirst_point или point_count "
			    "из второй строки заголовка");
		}
		header.time_start = header_params[2];
	}

	static void parse_channel_names(std::ifstream &file, SignalHeader &header) {
		auto header_params = read_data_line(file);
		if (header_params.empty()) {
			throw std::runtime_error("Неверный формат: отсутствуют названия каналов в "
			                         "третьей строке заголовка");
		}
		header.num_channels = static_cast<int32_t>(
		    std::min(header_params.size(), static_cast<size_t>(header.num_channels)));
		header.channel_names.resize(header.num_channels);
		for (int i = 0; i < header.num_channels; i++) {
			header.channel_names[i] = header_params[i];
		}
	}

	static void parse_lsbs(std::ifstream &file, SignalHeader &header) {
		auto header_params = read_data_line(file);
		if (header_params.size() < static_cast<size_t>(header.num_channels)) {
			throw std::runtime_error("Неверный формат: количество LSB значений не "
			                         "совпадает с количеством каналов");
		}
		header.lsbs.resize(header.num_channels);
		try {
			for (int i = 0; i < header.num_channels; i++) {
				header.lsbs[i] = std::stod(header_params[i]);
			}
		} catch (const std::exception &) {
			throw std::runtime_error("Неверный формат: не удается распарсить LSB "
			                         "значения для одного из каналов");
		}
	}
} // namespace signals