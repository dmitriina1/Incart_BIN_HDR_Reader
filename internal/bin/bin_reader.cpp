#include "bin_reader.hpp"

#include "../hdr/hdr_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

// Размер блока для чтения данных из BIN файла за одну операцию.
static constexpr int64_t BIN_READ_CHUNK_SIZE = 100000;

namespace signals {
	static void allocate_storage(SignalData &data, int64_t len);
	static void read_block(std::ifstream &file, std::vector<int32_t> &block_buf,
	                       int64_t points_to_read, int64_t num_channels);
	static void copy_raw_data(const std::vector<int32_t> &block_buf, SignalData &data,
	                          int64_t dest_offset, int64_t points_in_block);

	void bin_open(const std::filesystem::path &bin_path, BinReader &reader) {
		reader = BinReader{};
		auto base = std::filesystem::path(bin_path).replace_extension("");
		auto header_path = base;
		header_path.replace_extension(".hdr");
		reader.bin_path = base.replace_extension(".bin");

		hdr_read(header_path, reader.header);
		reader.file.open(reader.bin_path, std::ios::binary);
		if (!reader.file) {
			throw std::system_error(errno, std::generic_category(),
			                        std::format("Не удалось открыть BIN файл для чтения: {}",
			                                    reader.bin_path.string()));
		}

		if (reader.header.num_channels <= 0) {
			throw std::runtime_error("Количество каналов в заголовке должно быть больше нуля");
		}

		reader.total_points = reader.header.ilast_point - reader.header.ifirst_point + 1;
	}

	void bin_close(BinReader &reader) {
		if (reader.file.is_open()) {
			reader.file.close();
		}
		reader = BinReader{};
	}

	void bin_seek(BinReader &reader, int64_t point_off) {
		if (!reader.file.is_open()) {
			throw std::logic_error("BIN reader не открыт");
		}
		if (point_off < 0) {
			throw std::invalid_argument("Смещение точек должно быть неотрицательным");
		}
		if (point_off > reader.total_points) {
			point_off = reader.total_points;
		}

		const int64_t frame_size = static_cast<int64_t>(reader.header.num_channels) *
		                           static_cast<int64_t>(sizeof(int32_t));
		const int64_t bytes_offset = point_off * frame_size;

		reader.file.clear();
		reader.file.seekg(static_cast<std::streamoff>(bytes_offset));
		if (!reader.file) {
			throw std::runtime_error(
			    std::format("Не удалось установить позицию чтения в BIN файле (смещение {} байт)",
			                bytes_offset));
		}

		reader.current_point = point_off;
	}

	int64_t bin_read_data_chunk(BinReader &reader, int64_t point_cnt,
	                            std::vector<int32_t> &block_buf) {
		if (!reader.file.is_open()) {
			throw std::logic_error("BIN reader не открыт");
		}
		if (point_cnt == 0) {
			block_buf.clear();
			return 0;
		}

		const int64_t points_to_read =
		    normalize_range(reader.total_points, reader.current_point, point_cnt);
		if (points_to_read == 0) {
			block_buf.clear();
			return 0;
		}

		block_buf.resize(static_cast<size_t>(points_to_read) *
		                 static_cast<size_t>(reader.header.num_channels));
		read_block(reader.file, block_buf, points_to_read, reader.header.num_channels);
		reader.current_point += points_to_read;
		return points_to_read;
	}

	void bin_read(const std::filesystem::path &bin_path, int64_t point_off, int64_t point_cnt,
	              SignalData &data) {
		BinReader reader;
		bin_open(bin_path, reader);

		const int64_t points_to_read = normalize_range(reader.total_points, point_off, point_cnt);
		bin_seek(reader, point_off);

		data = SignalData{};
		static_cast<SignalHeader &>(data) = reader.header;
		data.ifirst_point = reader.header.ifirst_point + point_off;
		data.ilast_point = (points_to_read > 0) ? (data.ifirst_point + points_to_read - 1)
		                                        : (data.ifirst_point - 1);
		data.num_points = points_to_read;

		if (points_to_read > 0) {
			allocate_storage(data, points_to_read);

			std::vector<int32_t> block_buf;
			int64_t point_pos = 0;
			while (point_pos < points_to_read) {
				const int64_t block_points =
				    std::min<int64_t>(BIN_READ_CHUNK_SIZE, points_to_read - point_pos);
				const int64_t points_in_block =
				    bin_read_data_chunk(reader, block_points, block_buf);
				if (points_in_block == 0) {
					break;
				}
				copy_raw_data(block_buf, data, point_pos, points_in_block);
				point_pos += points_in_block;
			}
		}
		bin_close(reader);
	}

	static void allocate_storage(SignalData &data, int64_t len) {
		data.num_points = len;

		if (len == 0)
			return;

		try {
			data.storage.resize(static_cast<size_t>(data.num_channels) * static_cast<size_t>(len),
			                    0);
		} catch (const std::bad_alloc &) {
			throw std::system_error(
			    ENOMEM, std::generic_category(),
			    std::format("Недостаточно памяти для хранения данных: требуется {} байт",
			                static_cast<size_t>(data.num_channels) * static_cast<size_t>(len) *
			                    sizeof(int32_t)));
		}
	}

	static void read_block(std::ifstream &file, std::vector<int32_t> &block_buf,
	                       int64_t points_to_read, int64_t num_channels) {
		const auto bytes_to_read =
		    static_cast<std::streamsize>(points_to_read * num_channels * sizeof(int32_t));

		file.read(reinterpret_cast<char *>(block_buf.data()), bytes_to_read); // NOSONAR

		if (file.gcount() != bytes_to_read)
			throw std::runtime_error(std::format(
			    "Не удалось прочитать весь запрошенный блок данных из BIN файла (ожидалось {} "
			    "байт, а получено {} байт)",
			    bytes_to_read, file.gcount()));
	}

	static void copy_raw_data(const std::vector<int32_t> &block_buf, SignalData &data,
	                          int64_t dest_offset, int64_t points_in_block) {
		if (data.lsbs.empty())
			data.lsbs.assign(static_cast<size_t>(data.num_channels), data.lsb_default);

		const int64_t num_channels = data.num_channels;

		for (int ch = 0; ch < num_channels; ch++) {
			int32_t *destptr = data.raw_channel(ch) + dest_offset;
			for (int64_t s = 0; s < points_in_block; s++)
				destptr[s] =
				    block_buf[(static_cast<size_t>(s) * static_cast<size_t>(num_channels)) +
				              static_cast<size_t>(ch)];
		}
	}
} // namespace signals