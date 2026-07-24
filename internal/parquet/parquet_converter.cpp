#include "parquet_converter.hpp"

#include <limits>
#include <format>

namespace signals {
	static constexpr int PARQUET_SCHEMA_MAJOR = 1;
	static constexpr int PARQUET_SCHEMA_MINOR = 0;
	static constexpr int PARQUET_SCHEMA_PATCH = 0;

	// Параметры для задания размера row group и кэширования. Размер кэша равен 8 row groups, что
	// при 100к точек на row group и N количестве каналов будет весить 8*100_000*N*4 байт(с учетом
	// что у нас все в int32). )
	static constexpr int64_t DEFAULT_ROW_GROUP_POINTS = 100000;
	static constexpr size_t MAX_ROW_GROUP_CACHE = 8;

	static std::shared_ptr<arrow::Table> read_row_group(ParquetReader &reader, int row_group_index);
	static std::string generate_format_version();
	static std::shared_ptr<arrow::KeyValueMetadata> make_file_metadata(const SignalHeader &header);
	static std::shared_ptr<arrow::Schema> make_signal_schema(const SignalHeader &header);
	static arrow::Result<SignalHeader> read_parquet_metadata(ParquetReader &reader);
	static arrow::Status copy_column_from_table(const std::shared_ptr<arrow::Table> &table,
	                                            int channel, int64_t local_start,
	                                            int64_t copy_count, int64_t out_offset,
	                                            SignalData &data);

	arrow::Status parquet_open(const std::filesystem::path &path, ParquetReader &reader) {
		reader = ParquetReader{};
		reader.row_group_cache.clear();
		reader.row_group_cache_order.clear();
		ARROW_ASSIGN_OR_RAISE(reader.file, arrow::io::ReadableFile::Open(path.string()))
		ARROW_ASSIGN_OR_RAISE(reader.reader,
		                      parquet::arrow::OpenFile(reader.file, arrow::default_memory_pool()))
		reader.reader->set_use_threads(true);

		auto file_metadata = reader.reader->parquet_reader()->metadata();
		if (!file_metadata)
			return arrow::Status::Invalid("Не удалось получить метаданные Parquet файла");

		reader.num_row_groups = reader.reader->num_row_groups();
		reader.row_group_first_row.reserve(static_cast<size_t>(reader.num_row_groups));
		reader.row_group_row_count.reserve(static_cast<size_t>(reader.num_row_groups));

		int64_t row_offset = 0;
		for (int rg = 0; rg < reader.num_row_groups; rg++) {
			const int64_t rg_rows = file_metadata->RowGroup(rg)->num_rows();
			reader.row_group_first_row.push_back(row_offset);
			reader.row_group_row_count.push_back(rg_rows);
			row_offset += rg_rows;
		}

		return arrow::Status::OK();
	}

	void parquet_close(ParquetReader &reader) {
		reader.row_group_cache.clear();
		reader.row_group_cache_order.clear();
		reader.row_group_first_row.clear();
		reader.row_group_row_count.clear();
		reader.reader.reset();
		reader.file.reset();
		reader.num_row_groups = 0;
	}

	arrow::Status parquet_write_signals_data(const std::filesystem::path &bin_path,
	                                         const std::filesystem::path &parquet_path,
	                                         int64_t point_off, int64_t point_cnt,
	                                         int64_t row_group_points) {
		if (point_off < 0)
			return arrow::Status::Invalid("Смещение точек не может быть отрицательным");

		const int64_t real_row_group_points =
		    row_group_points > 0 ? row_group_points : DEFAULT_ROW_GROUP_POINTS;

		BinReader reader;
		try {
			bin_open(bin_path, reader);
		} catch (const std::exception &e) {
			return arrow::Status::IOError(std::format("Не удалось открыть BIN файл: {}", e.what()));
		}

		SignalHeader range_header;
		const int64_t points_to_write = make_header_for_range(reader.header, reader.total_points,
		                                                      point_off, point_cnt, range_header);

		const int32_t num_chan = range_header.num_channels;
		const std::shared_ptr<arrow::Schema> schema = make_signal_schema(range_header);

		parquet::WriterProperties::Builder builder;
		builder.compression(parquet::Compression::ZSTD);
		builder.encoding(parquet::Encoding::DELTA_BINARY_PACKED);
		builder.max_row_group_length(real_row_group_points);

		parquet::ArrowWriterProperties::Builder arrow_builder;
		arrow_builder.store_schema();

		ARROW_ASSIGN_OR_RAISE(auto output_file,
		                      arrow::io::FileOutputStream::Open(parquet_path.string()))
		ARROW_ASSIGN_OR_RAISE(auto writer, parquet::arrow::FileWriter::Open(
		                                       *schema, arrow::default_memory_pool(), output_file,
		                                       builder.build(), arrow_builder.build()))
		ARROW_RETURN_NOT_OK(writer->AddKeyValueMetadata(make_file_metadata(range_header)));

		std::vector<std::vector<int32_t>> buffers(
		    static_cast<size_t>(num_chan),
		    std::vector<int32_t>(static_cast<size_t>(real_row_group_points)));
		std::vector<int32_t> raw_chunk;
		raw_chunk.reserve(static_cast<size_t>(real_row_group_points) *
		                  static_cast<size_t>(num_chan));

		bin_seek(reader, point_off);
		int64_t points_written = 0;

		while (points_written < points_to_write) {
			const int64_t chunk_size =
			    std::min(real_row_group_points, points_to_write - points_written);
			const int64_t points_read = bin_read_data_chunk(reader, chunk_size, raw_chunk);
			if (points_read <= 0)
				break;

			for (int64_t p = 0; p < points_read; p++) {
				const size_t src = static_cast<size_t>(p) * static_cast<size_t>(num_chan);
				for (int ch = 0; ch < num_chan; ch++)
					buffers[static_cast<size_t>(ch)][static_cast<size_t>(p)] =
					    raw_chunk[src + static_cast<size_t>(ch)];
			}

			std::vector<std::shared_ptr<arrow::Array>> arrays;
			arrays.reserve(static_cast<size_t>(num_chan));
			for (int ch = 0; ch < num_chan; ch++) {
				arrow::Int32Builder intBuilder;
				ARROW_RETURN_NOT_OK(
				    intBuilder.AppendValues(buffers[static_cast<size_t>(ch)].data(), points_read));
				std::shared_ptr<arrow::Array> arr;
				ARROW_RETURN_NOT_OK(intBuilder.Finish(&arr));
				arrays.push_back(arr);
			}

			auto batch = arrow::RecordBatch::Make(schema, points_read, arrays);
			ARROW_RETURN_NOT_OK(writer->WriteRecordBatch(*batch));
			points_written += points_read;
		}

		if (points_written != points_to_write) {
			bin_close(reader);
			return arrow::Status::IOError(
			    "Не удалось записать весь запрошенный диапазон точек в Parquet");
		}

		bin_close(reader);
		return writer->Close();
	}

	void parquet_write_signal_data_like_stream(const std::filesystem::path &bin_path,
	                                           const std::filesystem::path &parquet_path,
	                                           int64_t point_off, int64_t point_cnt,
	                                           int64_t row_group_points) {
		auto status = parquet_write_signals_data(bin_path, parquet_path, point_off, point_cnt,
		                                         row_group_points);
		if (!status.ok()) {
			throw std::runtime_error(
			    std::format("Не удалось записать данные в parquet файл: {}", status.ToString()));
		}
	}

	arrow::Status parquet_read_signal_data_like_stream(ParquetReader &reader, SignalData &data,
	                                                   int64_t point_off, int64_t point_cnt) {
		if (point_off < 0)
			return arrow::Status::Invalid("Смещение не может быть отрицательным");

		if (reader.num_row_groups == 0)
			return arrow::Status::Invalid("Parquet файл не содержит row groups");

		ARROW_ASSIGN_OR_RAISE(auto metadata, read_parquet_metadata(reader))

		const int64_t total_rows = reader.reader->parquet_reader()->metadata()->num_rows();
		const int64_t points_to_read =
		    prepare_signal_data_window(metadata, total_rows, point_off, point_cnt, data, true);
		if (points_to_read == 0)
			return arrow::Status::OK();

		if (reader.row_group_first_row.size() != static_cast<size_t>(reader.num_row_groups) ||
		    reader.row_group_row_count.size() != static_cast<size_t>(reader.num_row_groups)) {
			return arrow::Status::Invalid("Parquet row-group метаданные некорректные");
		}

		const int64_t target_end = point_off + points_to_read;

		const auto first_it = std::ranges::upper_bound(reader.row_group_first_row, point_off);
		auto first_rg =
		    static_cast<int>(std::distance(reader.row_group_first_row.begin(), first_it));
		if (first_rg > 0)
			first_rg--;

		for (int rg = first_rg; rg < reader.num_row_groups; rg++) {
			const int64_t rg_start = reader.row_group_first_row[static_cast<size_t>(rg)];
			if (rg_start >= target_end)
				break;

			const int64_t rg_rows = reader.row_group_row_count[static_cast<size_t>(rg)];

			std::shared_ptr<arrow::Table> table = read_row_group(reader, rg);
			if (!table)
				return arrow::Status::Invalid(std::format("Не удалось прочитать row group {}", rg));

			const int64_t local_start = std::max<int64_t>(0, point_off - rg_start);
			const int64_t local_end = std::min<int64_t>(rg_rows, target_end - rg_start);
			const int64_t copy_count = local_end - local_start;

			const int64_t out_off = rg_start + local_start - point_off;

			for (int ch = 0; ch < data.num_channels; ch++) {
				ARROW_RETURN_NOT_OK(
				    copy_column_from_table(table, ch, local_start, copy_count, out_off, data));
			}
		}

		return arrow::Status::OK();
	}

	void parquet_read_signal_data_like_stream(const std::filesystem::path &parquet_path,
	                                          SignalData &data, int64_t point_off,
	                                          int64_t point_cnt) {
		static ParquetReader cached_reader;

		if (static std::filesystem::path cached_path;
		    cached_path != parquet_path || !cached_reader.reader) {
			parquet_close(cached_reader);
			if (auto status = parquet_open(parquet_path, cached_reader); !status.ok())
				throw std::runtime_error(
				    std::format("Не удалось открыть parquet файл: {}", status.ToString()));
			cached_path = parquet_path;
		}

		auto status =
		    parquet_read_signal_data_like_stream(cached_reader, data, point_off, point_cnt);
		if (!status.ok())
			throw std::runtime_error(
			    std::format("Не удалось прочитать parquet-файл: {}", status.ToString()));
	}

	// static
	static std::shared_ptr<arrow::Table> read_row_group(ParquetReader &reader,
	                                                    int row_group_index) {
		if (auto it = reader.row_group_cache.find(row_group_index);
		    it != reader.row_group_cache.end()) {
			reader.row_group_cache_order.erase(
			    std::ranges::find(reader.row_group_cache_order, row_group_index));
			reader.row_group_cache_order.push_back(row_group_index);
			return it->second;
		}

		// ReadRowGroup - deprecated, так что читаем через RecordBatchReader, который должен
		// использовать оптимизированные методы чтения row group std::shared_ptr<arrow::Table>
		// table; if (auto st = reader.reader->ReadRowGroup(row_group_index, &table); !st.ok() ||
		// !table)
		//     return nullptr;

		std::vector<int> row_groups{ row_group_index };

		auto batch_reader_result = reader.reader->GetRecordBatchReader(row_groups);
		if (!batch_reader_result.ok())
			return nullptr;

		std::unique_ptr<arrow::RecordBatchReader> batch_reader =
		    batch_reader_result.MoveValueUnsafe();

		if (!batch_reader)
			return nullptr;

		auto table_result = arrow::Table::FromRecordBatchReader(batch_reader.get());
		if (!table_result.ok())
			return nullptr;

		std::shared_ptr<arrow::Table> table = table_result.MoveValueUnsafe();
		if (!table)
			return nullptr;

		if (reader.row_group_cache.size() >= MAX_ROW_GROUP_CACHE) {
			int lru = reader.row_group_cache_order.front();
			reader.row_group_cache.erase(lru);
			reader.row_group_cache_order.erase(reader.row_group_cache_order.begin());
		}

		reader.row_group_cache[row_group_index] = table;
		reader.row_group_cache_order.push_back(row_group_index);
		return table;
	}

	static std::string generate_format_version() {
		return std::format("{}.{}.{}", PARQUET_SCHEMA_MAJOR, PARQUET_SCHEMA_MINOR,
		                   PARQUET_SCHEMA_PATCH);
	}

	static std::shared_ptr<arrow::KeyValueMetadata> make_file_metadata(const SignalHeader &header) {
		auto metadata = std::make_shared<arrow::KeyValueMetadata>();
		metadata->Append("format_name", "BINHDR");
		metadata->Append("format_version", generate_format_version());
		metadata->Append("frequency", std::to_string(header.frequency));
		metadata->Append("lsb_default", std::to_string(header.lsb_default));
		metadata->Append("offset", std::to_string(header.ifirst_point));
		metadata->Append("time_start", header.time_start);

		for (const auto &[key, value] : header.optional_fields) {
			if (std::holds_alternative<std::string>(value)) {
				metadata->Append("optional." + key, std::get<std::string>(value));
			} else if (std::holds_alternative<int64_t>(value)) {
				metadata->Append("optional." + key, std::to_string(std::get<int64_t>(value)));
			} else if (std::holds_alternative<double>(value)) {
				metadata->Append("optional." + key, std::to_string(std::get<double>(value)));
			}
		}

		return metadata;
	}

	static std::shared_ptr<arrow::Schema> make_signal_schema(const SignalHeader &header) {
		const int32_t num_channels = header.num_channels;
		std::vector<std::shared_ptr<arrow::Field>> fields;
		fields.reserve(static_cast<size_t>(num_channels));

		for (int32_t ch = 0; ch < num_channels; ch++) {
			const std::string channel_name = !header.channel_names[static_cast<size_t>(ch)].empty()
			                                     ? header.channel_names[static_cast<size_t>(ch)]
			                                     : "";
			const double lsb = (static_cast<size_t>(ch) < header.lsbs.size())
			                       ? header.lsbs[static_cast<size_t>(ch)]
			                       : header.lsb_default;
			const std::string unit = !header.units[static_cast<size_t>(ch)].empty()
			                             ? header.units[static_cast<size_t>(ch)]
			                             : "";

			auto field_metadata = std::make_shared<arrow::KeyValueMetadata>();
			field_metadata->Append("lsb", std::to_string(lsb));
			field_metadata->Append("offset", std::to_string(header.ifirst_point));
			field_metadata->Append("type", "int32");
			field_metadata->Append("unit", unit);

			fields.push_back(arrow::field(channel_name, arrow::int32(), false, field_metadata));
		}

		return arrow::schema(fields);
	}

	static arrow::Result<SignalHeader> read_parquet_metadata(ParquetReader &reader) {
		auto file_metadata = reader.reader->parquet_reader()->metadata();
		if (!file_metadata) {
			return arrow::Status::Invalid("Не удалось получить метаданные Parquet файла");
		}
		auto map = file_metadata->key_value_metadata();
		if (!map) {
			return arrow::Status::Invalid("Parquet файл не содержит ключ-значение метаданных");
		}

		auto get_field = [&](const std::string &key) {
			auto result = map->Get(key);
			return result.ok() ? *result : "";
		};

		SignalHeader metadata;
		metadata.num_channels = static_cast<int32_t>(file_metadata->schema()->num_columns());
		metadata.frequency = std::stod(get_field("frequency"));
		metadata.lsb_default = std::stod(get_field("lsb_default"));
		metadata.ifirst_point = std::stoll(get_field("offset"));
		metadata.ilast_point = metadata.ifirst_point + file_metadata->num_rows() - 1;
		metadata.time_start = get_field("time_start");

		std::shared_ptr<arrow::Schema> schema;
		ARROW_RETURN_NOT_OK(reader.reader->GetSchema(&schema));

		metadata.channel_names.reserve(metadata.num_channels);
		metadata.lsbs.reserve(metadata.num_channels);
		metadata.units.reserve(metadata.num_channels);

		for (int32_t ch = 0; ch < metadata.num_channels; ch++) {
			auto field = schema->field(ch);
			auto field_map = field->metadata();
			metadata.channel_names.push_back(field->name());
			metadata.lsbs.push_back(std::stod(field_map->Get("lsb").ValueOrDie()));
			metadata.units.push_back(field_map->Get("unit").ValueOrDie());
		}
		return metadata;
	}

	static arrow::Status copy_column_from_table(const std::shared_ptr<arrow::Table> &table,
	                                            int channel, int64_t local_start,
	                                            int64_t copy_count, int64_t out_offset,
	                                            SignalData &data) {
		int32_t *dst = data.raw_channel(channel) + out_offset;
		auto chunked = table->column(channel);
		int64_t skip = local_start;
		int64_t written = 0;

		for (const auto &chunk : chunked->chunks()) {
			const int64_t len = chunk ? chunk->length() : 0;
			if (skip >= len) {
				skip -= len;
				continue;
			}
			const int64_t take = std::min(copy_count - written, len - skip);
			auto int_arr = std::dynamic_pointer_cast<arrow::Int32Array>(chunk);

			const int32_t *src = int_arr->raw_values() + skip;
			for (int64_t p = 0; p < take; p++) {
				dst[written + p] = src[p];
			}
			written += take;
			skip = 0;
			if (written == copy_count)
				break;
		}
		return arrow::Status::OK();
	}
} // namespace signals