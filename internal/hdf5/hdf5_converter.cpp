#include "hdf5_converter.hpp"

#include <H5Cpp.h>
#include <limits>
#include <unordered_set>

namespace signals {
	static constexpr int H5F_SCHEMA_MAJOR = 1;
	static constexpr int H5F_SCHEMA_MINOR = 0;
	static constexpr int H5F_SCHEMA_PATCH = 0;

	// Уровень сжатия для HDF5 (0-9, где 0 - без сжатия, 9 - максимальное сжатие), путем
	// тестирования выбрана 1, т.к. 9 значительно увеличивает время записи без заметного уменьшения
	// размера файла по сравнению с 1.
	const int HDF5_DEFLATE_LEVEL = 1;
	const int64_t HDF5_CHUNK_POINTS = 100000;

	static H5::FileAccPropList make_access_props();
	static H5::H5File *get_open_hdf5_file(void *reader_handle);
	static std::string generate_format_version();

	void *hdf5_open(const std::filesystem::path &hdf5_path) {
		H5::FileAccPropList access_props = make_access_props();
		auto *file = std::make_unique<H5::H5File>(H5std_string(hdf5_path.string()), H5F_ACC_RDONLY,
		                                          H5::FileCreatPropList::DEFAULT, access_props)
		                 .release();

		return static_cast<void *>(file);
	}

	void hdf5_close(void *&reader_handle) {
		if (!reader_handle) {
			return;
		}

		auto *file = static_cast<H5::H5File *>(reader_handle);
		file->close();

		reader_handle = nullptr;
	}

	void hdf5_read_signal_data_like_stream(void *reader_handle, SignalData &data, int64_t point_off,
	                                       int64_t point_cnt, int64_t chunk_points) {
		const auto *file = get_open_hdf5_file(reader_handle);
		hdf5_read_signal_data_like_stream(*file, data, point_off, point_cnt, chunk_points);
	}

	void hdf5_write_signal_header(const H5::H5File &file, const SignalHeader &header) {
		H5::Group data_group = file.createGroup("/data");
		data_group.createAttribute("format_name", H5::StrType(0, 6), H5::DataSpace(H5S_SCALAR))
		    .write(H5::StrType(0, 6), "BINHDR");
		data_group
		    .createAttribute("format_version", H5::StrType(0, H5T_VARIABLE),
		                     H5::DataSpace(H5S_SCALAR))
		    .write(H5::StrType(0, H5T_VARIABLE), generate_format_version());

		data_group
		    .createAttribute("frequency", H5::PredType::NATIVE_DOUBLE, H5::DataSpace(H5S_SCALAR))
		    .write(H5::PredType::NATIVE_DOUBLE, &header.frequency);
		data_group
		    .createAttribute("lsb_default", H5::PredType::NATIVE_DOUBLE, H5::DataSpace(H5S_SCALAR))
		    .write(H5::PredType::NATIVE_DOUBLE, &header.lsb_default);
		data_group.createAttribute("offset", H5::PredType::NATIVE_INT64, H5::DataSpace(H5S_SCALAR))
		    .write(H5::PredType::NATIVE_INT64, &header.ifirst_point);

		const std::string timestart = header.time_start.empty() ? "" : header.time_start;
		data_group.createAttribute("time_start", H5::StrType(0, 19), H5::DataSpace(H5S_SCALAR))
		    .write(H5::StrType(0, 19), timestart);

		hsize_t dims[] = { static_cast<hsize_t>(header.num_channels) };
		H5::DataSpace universal_space(1, dims);

		std::vector<const char *> channel_name_ptrs;
		channel_name_ptrs.reserve(static_cast<size_t>(header.num_channels));

		for (int32_t ch = 0; ch < header.num_channels; ch++) {
			const auto ch_idx = static_cast<size_t>(ch);

			if (ch_idx < header.channel_names.size() && !header.channel_names[ch_idx].empty()) {
				channel_name_ptrs.push_back(header.channel_names[ch_idx].c_str());
			} else {
				channel_name_ptrs.push_back("");
			}
		}

		data_group.createAttribute("channel_names", H5::StrType(0, H5T_VARIABLE), universal_space)
		    .write(H5::StrType(0, H5T_VARIABLE),
		           static_cast<const void *>(channel_name_ptrs.data()));

		if (header.optional_fields.empty()) {
			return;
		}
		for (const auto &[key, value] : header.optional_fields) {
			if (std::holds_alternative<std::string>(value)) {
				const auto &str_value = std::get<std::string>(value);
				data_group
				    .createAttribute(key, H5::StrType(0, H5T_VARIABLE), H5::DataSpace(H5S_SCALAR))
				    .write(H5::StrType(0, H5T_VARIABLE), str_value);
			} else if (std::holds_alternative<int64_t>(value)) {
				int64_t int_value = std::get<int64_t>(value);
				data_group
				    .createAttribute(key, H5::PredType::NATIVE_INT64, H5::DataSpace(H5S_SCALAR))
				    .write(H5::PredType::NATIVE_INT64, &int_value);
			} else if (std::holds_alternative<double>(value)) {
				double double_value = std::get<double>(value);
				data_group
				    .createAttribute(key, H5::PredType::NATIVE_DOUBLE, H5::DataSpace(H5S_SCALAR))
				    .write(H5::PredType::NATIVE_DOUBLE, &double_value);
			}
		}
	}

	void hdf5_write_bin_data(const H5::H5File &file, BinReader &reader, int64_t point_off,
	                         int64_t point_cnt) {
		const int32_t num_channels = reader.header.num_channels;
		const int64_t points_to_read = normalize_range(reader.total_points, point_off, point_cnt);
		if (points_to_read == 0) {
			return;
		}

		const int64_t block_points = std::min<int64_t>(HDF5_CHUNK_POINTS, points_to_read);
		hsize_t total_dims[1] = { static_cast<hsize_t>(points_to_read) };
		hsize_t chunk_dims[1] = { static_cast<hsize_t>(block_points) };

		H5::DSetCreatPropList proplist_params;
		proplist_params.setChunk(1, chunk_dims);
		proplist_params.setShuffle();
		if (H5Zfilter_avail(H5Z_FILTER_DEFLATE) > 0) {
			proplist_params.setDeflate(HDF5_DEFLATE_LEVEL);
		}

		const int64_t first_point = reader.header.ifirst_point + point_off;

		std::vector<H5::DataSet> channel_datasets;
		channel_datasets.reserve(static_cast<size_t>(num_channels));

		H5::Group data_group = file.openGroup("/data");

		for (int32_t ch = 0; ch < num_channels; ch++) {
			H5::DataSpace dataset_space(1, total_dims);
			H5::DataSet dataset = data_group.createDataSet(reader.header.channel_names[ch],
			                                               H5::PredType::NATIVE_INT32,
			                                               dataset_space, proplist_params);

			const double lsb = (static_cast<size_t>(ch) < reader.header.lsbs.size())
			                       ? reader.header.lsbs[static_cast<size_t>(ch)]
			                       : reader.header.lsb_default;
			const std::string unit = static_cast<size_t>(ch) < reader.header.units.size()
			                             ? reader.header.units[static_cast<size_t>(ch)]
			                             : "";

			dataset.createAttribute("lsb", H5::PredType::NATIVE_DOUBLE, H5S_SCALAR)
			    .write(H5::PredType::NATIVE_DOUBLE, &lsb);
			dataset.createAttribute("offset", H5::PredType::NATIVE_INT64, H5S_SCALAR)
			    .write(H5::PredType::NATIVE_INT64, &first_point);
			dataset.createAttribute("type", H5::StrType(0, 5), H5::DataSpace(H5S_SCALAR))
			    .write(H5::StrType(0, 5), "int32");
			dataset.createAttribute("unit", H5::StrType(0, H5T_VARIABLE), H5S_SCALAR)
			    .write(H5::StrType(0, H5T_VARIABLE), unit);

			channel_datasets.push_back(dataset);
		}

		std::vector<int32_t> chunk_buffer;
		std::vector<int32_t> channel_buffer(static_cast<size_t>(block_points));
		bin_seek(reader, point_off);

		int64_t points_written = 0;
		while (points_written < points_to_read) {
			const int64_t request_points =
			    std::min<int64_t>(block_points, points_to_read - points_written);
			const int64_t points_in_block =
			    bin_read_data_chunk(reader, request_points, chunk_buffer);
			if (points_in_block == 0)
				break;

			hsize_t start[1] = { static_cast<hsize_t>(points_written) };
			hsize_t count[1] = { static_cast<hsize_t>(points_in_block) };
			H5::DataSpace mem_space(1, count);

			for (int32_t ch = 0; ch < num_channels; ch++) {
				for (int64_t point = 0; point < points_in_block; point++) {
					channel_buffer[static_cast<size_t>(point)] =
					    chunk_buffer[(static_cast<size_t>(point) *
					                  static_cast<size_t>(num_channels)) +
					                 static_cast<size_t>(ch)];
				}

				H5::DataSpace channel_space = channel_datasets[static_cast<size_t>(ch)].getSpace();
				channel_space.selectHyperslab(H5S_SELECT_SET, count, start);
				channel_datasets[static_cast<size_t>(ch)].write(
				    channel_buffer.data(), H5::PredType::NATIVE_INT32, mem_space, channel_space);
			}

			points_written += points_in_block;
		}

		if (points_written != points_to_read)
			throw std::runtime_error("Не удалось записать весь запрошенный диапазон точек в HDF5");
	}

	void hdf5_write_signals_data(const std::filesystem::path &bin_path,
	                             const std::filesystem::path &hdf5_path, int64_t point_off,
	                             int64_t point_cnt) {
		BinReader reader;
		bin_open(bin_path, reader);

		SignalHeader range_header;
		const int64_t points_to_write = make_header_for_range(reader.header, reader.total_points,
		                                                      point_off, point_cnt, range_header);

		H5std_string file_name(hdf5_path.string());
		H5::FileAccPropList access_props = make_access_props();
		H5::H5File file(file_name, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, access_props);
		hdf5_write_signal_header(file, range_header);

		if (points_to_write == 0) {
			bin_close(reader);
			return;
		}

		try {
			hdf5_write_bin_data(file, reader, point_off, point_cnt);
		} catch (...) {
			bin_close(reader);
			throw;
		}

		bin_close(reader);
	}

	void hdf5_read_signal_header_metadata(const H5::H5File &file, SignalHeader &metadata) {
		H5::Group data_group = file.openGroup("/data");
		data_group.openAttribute("frequency")
		    .read(H5::PredType::NATIVE_DOUBLE, &metadata.frequency);
		data_group.openAttribute("lsb_default")
		    .read(H5::PredType::NATIVE_DOUBLE, &metadata.lsb_default);
		data_group.openAttribute("offset").read(H5::PredType::NATIVE_INT64, &metadata.ifirst_point);

		H5::Attribute time_start_attribute = data_group.openAttribute("time_start");
		std::string time_start;
		time_start_attribute.read(H5::StrType(0, 19), time_start);
		metadata.time_start = time_start.empty() ? "" : time_start;
		data_group.close();
	}

	bool is_field_optional(const std::string &key) {
		static const std::unordered_set<std::string> unoptional_keys = { "frequency", "lsb_default",
			                                                             "offset", "time_start",
			                                                             "channel_names" };
		return unoptional_keys.contains(key);
	}

	void hdf5_read_signal_header_optional_params(const H5::H5File &file, SignalHeader &metadata) {
		H5::Group data_group = file.openGroup("/data");
		for (int i = 0; i < data_group.getNumAttrs(); i++) {
			H5::Attribute attr = data_group.openAttribute(i);
			std::string key = attr.getName();
			H5::DataType type = attr.getDataType();

			if (is_field_optional(attr.getName()))
				continue;

			if (type.getClass() == H5T_STRING) {
				std::string str_value;
				attr.read(type, str_value);
				metadata.optional_fields[key] = str_value;
			} else if (type.getClass() == H5T_INTEGER) {
				int64_t int_value;
				attr.read(type, &int_value);
				metadata.optional_fields[key] = int_value;
			} else if (type.getClass() == H5T_FLOAT) {
				double double_value;
				attr.read(type, &double_value);
				metadata.optional_fields[key] = double_value;
			}
		}
		data_group.close();
	}

	void hdf5_read_channels_metadata(const H5::H5File &file, SignalHeader &metadata) {
		H5::Group data_group = file.openGroup("/data");
		H5::Attribute chan_names_dataset = data_group.openAttribute("channel_names");

		H5::DataSet first_dataset = data_group.openDataSet(data_group.getObjnameByIdx(0));
		H5::DataSpace first_space = first_dataset.getSpace();
		hsize_t dims[1];
		first_space.getSimpleExtentDims(dims);

		metadata.ilast_point = metadata.ifirst_point + static_cast<int64_t>(dims[0]) - 1;

		std::vector<H5::Attribute> lsbs_dataset;
		std::vector<H5::Attribute> units_dataset;

		H5::DataSpace space = chan_names_dataset.getSpace();
		hsize_t num_chan = 0;
		space.getSimpleExtentDims(&num_chan);
		metadata.num_channels = static_cast<int32_t>(num_chan);

		std::vector<char *> chan_names(num_chan, nullptr);
		chan_names_dataset.read(H5::StrType(0, H5T_VARIABLE),
		                        static_cast<void *>(chan_names.data()));

		metadata.lsbs.clear();
		metadata.units.clear();

		for (int32_t ch = 0; ch < metadata.num_channels; ch++) {
			std::string channel_name(chan_names[static_cast<size_t>(ch)]);
			H5::DataSet channel_dataset = data_group.openDataSet(channel_name);

			H5::Attribute lsb_attr = channel_dataset.openAttribute("lsb");
			double lsb_value;
			lsb_attr.read(H5::PredType::NATIVE_DOUBLE, &lsb_value);
			metadata.lsbs.push_back(lsb_value);

			H5::Attribute unit_attr = channel_dataset.openAttribute("unit");
			char *unit_value = nullptr;
			unit_attr.read(H5::StrType(0, H5T_VARIABLE), static_cast<void *>(&unit_value));
			metadata.units.emplace_back(unit_value ? unit_value : "");
			if (unit_value)
				H5free_memory(unit_value);
		}

		metadata.channel_names.clear();

		for (hsize_t i = 0; i < num_chan; i++) {
			metadata.channel_names.emplace_back(chan_names[i] ? chan_names[i] : "");
		}

		H5::StrType str_type(0, H5T_VARIABLE);
		H5Treclaim(str_type.getId(), space.getId(), H5P_DEFAULT,
		           static_cast<void *>(chan_names.data()));
		data_group.close();
	}

	void hdf5_read_signal_data_like_stream(const H5::H5File &file, SignalData &data,
	                                       int64_t point_off, int64_t point_cnt,
	                                       int64_t chunk_points) {
		if (chunk_points <= 0) {
			throw std::invalid_argument("Количество точек в чанке должно быть положительным");
		}

		if (point_off < 0) {
			throw std::invalid_argument("Смещение точек не может быть отрицательным");
		}

		SignalHeader metadata;
		hdf5_read_signal_header_metadata(file, metadata);
		hdf5_read_signal_header_optional_params(file, metadata);
		hdf5_read_channels_metadata(file, metadata);
		const int64_t total_points = std::max<int64_t>(0, metadata.total_points());
		const int64_t points_to_read =
		    prepare_signal_data_window(metadata, total_points, point_off, point_cnt, data, false);

		if (points_to_read == 0)
			return;

		H5::Group data_group = file.openGroup("/data");
		std::vector<H5::DataSet> channel_datasets;
		channel_datasets.reserve(static_cast<size_t>(data.num_channels));

		for (int32_t ch = 0; ch < data.num_channels; ch++) {
			const std::string &channel_name = data.channel_names[static_cast<size_t>(ch)];

			H5::DataSet dataset = data_group.openDataSet(channel_name);
			channel_datasets.push_back(dataset);
		}

		const int64_t actual_chunk = std::min<int64_t>(chunk_points, points_to_read);

		std::vector<int32_t> channel_buffer(static_cast<size_t>(actual_chunk));
		int64_t points_read = 0;
		while (points_read < points_to_read) {
			const int64_t points_in_block =
			    std::min<int64_t>(actual_chunk, points_to_read - points_read);
			hsize_t start[1] = { static_cast<hsize_t>(point_off + points_read) };
			hsize_t count[1] = { static_cast<hsize_t>(points_in_block) };

			H5::DataSpace mem_space(1, count);

			for (int32_t ch = 0; ch < data.num_channels; ch++) {
				H5::DataSpace file_space = channel_datasets[static_cast<size_t>(ch)].getSpace();
				file_space.selectHyperslab(H5S_SELECT_SET, count, start);
				channel_datasets[static_cast<size_t>(ch)].read(
				    channel_buffer.data(), H5::PredType::NATIVE_INT32, mem_space, file_space);

				int32_t *dest = data.raw_channel(ch) + points_read;
				for (int64_t point = 0; point < points_in_block; point++) {
					dest[point] = channel_buffer[static_cast<size_t>(point)];
				}
			}

			points_read += points_in_block;
		}
	}

	void hdf5_read_signal_data_like_stream(const std::filesystem::path &hdf5_path, SignalData &data,
	                                       int64_t point_off, int64_t point_cnt,
	                                       int64_t chunk_points) {
		static void *cached_reader_handle = nullptr;

		if (static std::filesystem::path cached_path;
		    cached_path != hdf5_path || !cached_reader_handle) {
			hdf5_close(cached_reader_handle);
			cached_reader_handle = hdf5_open(hdf5_path);
			cached_path = hdf5_path;
		}

		hdf5_read_signal_data_like_stream(cached_reader_handle, data, point_off, point_cnt,
		                                  chunk_points);
	}

	static H5::FileAccPropList make_access_props() {
		H5::FileAccPropList props;
		const unsigned mdc_nelmts = HDF5_CHUNK_POINTS;
		const size_t rdcc_nbytes = 1024ULL * 1024ULL;
		const size_t rdcc_w0 = 1;
		props.setCache(mdc_nelmts, 1024, rdcc_nbytes, rdcc_w0);
		return props;
	}

	static H5::H5File *get_open_hdf5_file(void *reader_handle) {
		if (!reader_handle) {
			throw std::logic_error("HDF5 файл не открыт");
		}
		return static_cast<H5::H5File *>(reader_handle);
	}

	static std::string generate_format_version() {
		return std::format("{}.{}.{}", H5F_SCHEMA_MAJOR, H5F_SCHEMA_MINOR, H5F_SCHEMA_PATCH);
	}
} // namespace signals