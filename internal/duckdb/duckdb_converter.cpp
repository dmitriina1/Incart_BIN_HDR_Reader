#include "duckdb_converter.hpp"

#include <limits>

namespace signals {
	static constexpr int DUCKDB_SCHEMA_MAJOR = 1;
	static constexpr int DUCKDB_SCHEMA_MINOR = 0;
	static constexpr int DUCKDB_SCHEMA_PATCH = 0;

	static std::string duckdb_result_error_message(duckdb_result &res, const char *fallback);
	static void create_bin_data_table(duckdb_connection conn, SignalHeader &header);
	static void duckdb_append_signal_header(duckdb_connection conn, const SignalHeader &header);
	static void duckdb_append_channels_data(duckdb_connection conn, const SignalHeader &header);
	static std::string create_select_query_for_signal_data(const SignalData &data);
	static void append_points_block(duckdb_appender points_appender,
	                                const std::vector<int32_t> &chunk_buffer,
	                                int64_t points_in_block, int32_t num_channels,
	                                int64_t first_point_id_in_block);
	static void duckdb_read_signal_metadata(duckdb_connection conn, SignalHeader &metadata);
	static void open_and_set_config(duckdb_config &config, const std::filesystem::path &duckdb_path,
	                                duckdb_database &db, signals::BinReader &reader,
	                                duckdb_connection &conn);
	static void transfer_chunk_to_channel_data(duckdb_data_chunk &chunk, duckdb_result &res,
	                                           signals::SignalData &data,
	                                           duckdb_prepared_statement &stmt, int64_t &offset,
	                                           const int64_t points_to_read);

	void create_db_schema(duckdb_connection conn, SignalHeader &header) {
		duckdb_result res;

		if (duckdb_query(conn,
		                 "CREATE TABLE IF NOT EXISTS signals ("
		                 "  frequency   DOUBLE,"
		                 "  lsb_default  DOUBLE,"
		                 "  \"offset\" BIGINT,"
		                 "  time_start   TIMESTAMP,"
		                 "  format_name  VARCHAR,"
		                 "  format_version VARCHAR,"
		                 "  optional_params JSON"
		                 ");",
		                 &res) != DuckDBSuccess) {
			std::string error_message =
			    duckdb_result_error_message(res, "Не удалось создать таблицу signals");
			duckdb_destroy_result(&res);
			throw std::runtime_error("Не удалось создать таблицу signals: " + error_message);
		}
		duckdb_destroy_result(&res);

		if (duckdb_query(conn,
		                 "CREATE TABLE IF NOT EXISTS channels ("
		                 "  name       VARCHAR,"
		                 "  lsb        DOUBLE,"
		                 "  unit       VARCHAR"
		                 ");",
		                 &res) != DuckDBSuccess) {
			std::string error_message =
			    duckdb_result_error_message(res, "Не удалось создать таблицу channels");
			duckdb_destroy_result(&res);
			throw std::runtime_error("Не удалось создать таблицу channels: " + error_message);
		}
		duckdb_destroy_result(&res);

		create_bin_data_table(conn, header);
	}

	static void duckdb_read_signal_data_like_stream_conn(duckdb_connection conn, SignalData &data,
	                                                     int64_t point_off, int64_t point_cnt) {
		if (point_off < 0) {
			throw std::invalid_argument("Смещение точек не может быть отрицательным");
		}

		SignalHeader metadata;
		duckdb_read_signal_metadata(conn, metadata);

		const int64_t total_points = std::max<int64_t>(0, metadata.total_points());
		const int64_t points_to_read =
		    prepare_signal_data_window(metadata, total_points, point_off, point_cnt, data, false);

		if (points_to_read == 0) {
			return;
		}

		std::string sql_query = create_select_query_for_signal_data(data);

		duckdb_prepared_statement stmt;
		if (duckdb_prepare(conn, sql_query.c_str(), &stmt) == DuckDBError) {
			const char *prepare_error = duckdb_prepare_error(stmt);
			std::string error_message = (prepare_error && prepare_error[0] != '\0')
			                                ? prepare_error
			                                : "DuckDB не вернул текст ошибки";
			duckdb_destroy_prepare(&stmt);
			throw std::runtime_error(
			    std::format("Не удалось подготовить запрос для чтения точек: {}", error_message));
		}

		duckdb_bind_int64(stmt, 1, data.ifirst_point);
		duckdb_bind_int64(stmt, 2, data.ilast_point);

		duckdb_result res;

		if (duckdb_execute_prepared_streaming(stmt, &res) == DuckDBError) {
			std::string error_message =
			    duckdb_result_error_message(res, "DuckDB не вернул текст ошибки");
			duckdb_destroy_result(&res);
			duckdb_destroy_prepare(&stmt);
			throw std::runtime_error(
			    std::format("Не удалось выполнить запрос для чтения точек: {}", error_message));
		}

		duckdb_data_chunk chunk;
		int64_t offset = 0;

		transfer_chunk_to_channel_data(chunk, res, data, stmt, offset, points_to_read);

		duckdb_destroy_result(&res);
		duckdb_destroy_prepare(&stmt);

		if (offset != points_to_read) {
			throw std::runtime_error(
			    std::format("Не удалось прочитать весь диапазон, ожидалось {} точек, а получили {}",
			                points_to_read, offset));
		}
	}

	void duckdb_read_signal_data_like_stream(const std::filesystem::path &duckdb_path,
	                                         SignalData &data, int64_t point_off,
	                                         int64_t point_cnt) {
		duckdb_database db = nullptr;
		duckdb_connection conn = nullptr;

		if (duckdb_open(duckdb_path.string().c_str(), &db) == DuckDBError) {
			throw std::runtime_error(
			    std::format("Не удалось открыть DuckDB: {}", duckdb_path.string()));
		}

		if (duckdb_connect(db, &conn) == DuckDBError) {
			duckdb_close(&db);
			throw std::runtime_error("Не удалось подключиться к DuckDB");
		}

		try {
			duckdb_read_signal_data_like_stream_conn(conn, data, point_off, point_cnt);
		} catch (...) {
			duckdb_disconnect(&conn);
			duckdb_close(&db);
			throw;
		}

		duckdb_disconnect(&conn);
		duckdb_close(&db);
	}

	static void duckdb_write_signal_data_chunked(const std::filesystem::path &bin_path,
	                                             const std::filesystem::path &duckdb_path,
	                                             int64_t point_off, int64_t point_cnt,
	                                             int64_t chunk_points,
	                                             int64_t commit_every_n_points) {
		BinReader reader;
		bin_open(bin_path, reader);

		duckdb_database db = nullptr;
		duckdb_connection conn = nullptr;
		duckdb_config config = nullptr;

		open_and_set_config(config, duckdb_path, db, reader, conn);

		try {
			if (chunk_points <= 0)
				throw std::invalid_argument("Количество точек в чанке должно быть больше нуля");
			if (point_off < 0)
				throw std::invalid_argument("Смещение точек должно быть неотрицательным");
			if (reader.header.num_channels <= 0) {
				bin_close(reader);
				throw std::runtime_error(
				    "Количество каналов в исходных данных должно быть больше нуля");
			}

			SignalHeader range_header;
			const int64_t points_to_read = make_header_for_range(
			    reader.header, reader.total_points, point_off, point_cnt, range_header);
			const int64_t first_point_id = range_header.ifirst_point;

			create_db_schema(conn, reader.header);
			duckdb_append_signal_header(conn, range_header);
			duckdb_append_channels_data(conn, range_header);

			const int64_t block_points = std::min<int64_t>(chunk_points, points_to_read);
			std::vector<int32_t> chunk_buffer;

			bin_seek(reader, point_off);

			int64_t points_read_total = 0;
			int64_t points_since_commit = 0;
			duckdb_appender points_appender = nullptr;

			auto start_transaction = [&]() {
				duckdb_result res;
				if (duckdb_query(conn, "BEGIN TRANSACTION;", &res) != DuckDBSuccess) {
					std::string error_message =
					    duckdb_result_error_message(res, "DuckDB не вернул текст ошибки");
					duckdb_destroy_result(&res);
					throw std::runtime_error(
					    std::format("Не удалось начать транзакцию: {}", error_message));
				}
				duckdb_destroy_result(&res);

				if (duckdb_appender_create(conn, nullptr, "points", &points_appender) ==
				    DuckDBError)
					throw std::runtime_error(
					    "Не удалось создать DuckDB appender для таблицы points");
			};

			auto commit_and_reopen = [&]() {
				duckdb_result res;
				if (duckdb_appender_flush(points_appender) == DuckDBError) {
					duckdb_error_data error = duckdb_appender_error_data(points_appender);
					std::string error_message = duckdb_error_data_message(error);
					duckdb_destroy_error_data(&error);
					throw std::runtime_error(std::format(
					    "Не удалось сбросить DuckDB appender points: {}", error_message));
				}
				duckdb_appender_destroy(&points_appender);

				if (duckdb_query(conn, "COMMIT;", &res) != DuckDBSuccess) {
					std::string error_message =
					    duckdb_result_error_message(res, "DuckDB не вернул текст ошибки");
					duckdb_destroy_result(&res);
					throw std::runtime_error(
					    std::format("Не удалось зафиксировать транзакцию: {}", error_message));
				}
				duckdb_destroy_result(&res);
			};

			start_transaction();

			while (points_read_total < points_to_read) {
				const int64_t request_points =
				    std::min<int64_t>(block_points, points_to_read - points_read_total);
				const int64_t points_in_block =
				    bin_read_data_chunk(reader, request_points, chunk_buffer);
				if (points_in_block == 0)
					break;

				append_points_block(points_appender, chunk_buffer, points_in_block,
				                    reader.header.num_channels, first_point_id + points_read_total);

				points_read_total += points_in_block;
				points_since_commit += points_in_block;

				if (points_since_commit >= commit_every_n_points &&
				    points_read_total < points_to_read) {
					commit_and_reopen();
					start_transaction();
					points_since_commit = 0;
				}
			}

			if (points_read_total != points_to_read)
				throw std::runtime_error(
				    "Не удалось прочитать весь запрошенный диапазон точек из BIN файла");

			commit_and_reopen();
			bin_close(reader);
		} catch (...) {
			duckdb_disconnect(&conn);
			duckdb_close(&db);
			bin_close(reader);
			throw;
		}

		duckdb_disconnect(&conn);
		duckdb_close(&db);
	}

	void duckdb_write_signal_data_like_stream(const std::filesystem::path &bin_path,
	                                          const std::filesystem::path &duckdb_path,
	                                          int64_t point_off, int64_t point_cnt,
	                                          int64_t chunk_points, int64_t commit_every_n_points) {
		try {
			duckdb_write_signal_data_chunked(bin_path, duckdb_path, point_off, point_cnt,
			                                 chunk_points, commit_every_n_points);
		} catch (const std::invalid_argument &) {
			throw;
		} catch (const std::logic_error &) {
			throw;
		} catch (const std::exception &ex) {
			throw std::runtime_error(
			    std::format("Не удалось записать данные в DuckDB: {}", std::string(ex.what())));
		}
	}

	// static
	static std::string duckdb_result_error_message(duckdb_result &res, const char *fallback) {
		if (const char *error = duckdb_result_error(&res); error && error[0] != '\0') {
			return error;
		}
		return fallback;
	}

	static void create_bin_data_table(duckdb_connection conn, SignalHeader &header) {
		duckdb_result res;
		std::string sql_query;

		sql_query = "CREATE TABLE IF NOT EXISTS points (id BIGINT";

		for (int i = 0; i < header.num_channels; i++) {
			std::string channel_name = !header.channel_names[i].empty() ? header.channel_names[i]
			                                                            : std::format("Ch {}", i);
			sql_query += ", " + channel_name + " INTEGER";
		}

		sql_query += ");";

		if (duckdb_query(conn, sql_query.c_str(), &res) != DuckDBSuccess) {
			std::string error_message =
			    duckdb_result_error_message(res, "Не удалось создать таблицу points");
			duckdb_destroy_result(&res);
			throw std::runtime_error(
			    std::format("Не удалось создать таблицу points: {}", error_message));
		}
		duckdb_destroy_result(&res);
	}

	static std::string generate_format_version() {
		return std::format("{}.{}.{}", DUCKDB_SCHEMA_MAJOR, DUCKDB_SCHEMA_MINOR,
		                   DUCKDB_SCHEMA_PATCH);
	}

	static void duckdb_append_signal_header(duckdb_connection conn, const SignalHeader &header) {
		duckdb_appender signals_appender = nullptr;
		if (duckdb_appender_create(conn, nullptr, "signals", &signals_appender) == DuckDBError) {
			throw std::runtime_error("Не удалось создать DuckDB appender для signals таблицы");
		}

		std::string optional_json_data = serialize_to_json(header.optional_fields);

		duckdb_appender_begin_row(signals_appender);
		duckdb_append_double(signals_appender, header.frequency);
		duckdb_append_double(signals_appender, header.lsb_default);
		duckdb_append_int64(signals_appender, header.ifirst_point);
		duckdb_append_varchar(signals_appender, header.time_start.c_str());
		duckdb_append_varchar(signals_appender, "BINHDR");
		duckdb_append_varchar(signals_appender, generate_format_version().c_str());
		duckdb_append_varchar(signals_appender, optional_json_data.c_str());
		duckdb_appender_end_row(signals_appender);

		if (duckdb_appender_flush(signals_appender) == DuckDBError) {
			duckdb_error_data error = duckdb_appender_error_data(signals_appender);
			std::string error_message = duckdb_error_data_message(error);
			duckdb_destroy_error_data(&error);
			duckdb_appender_destroy(&signals_appender);
			throw std::runtime_error(
			    std::format("Не удалось записать метаданные сигнала в DuckDB: {}", error_message));
		}
		duckdb_appender_destroy(&signals_appender);
	}

	static void duckdb_append_channels_data(duckdb_connection conn, const SignalHeader &header) {
		duckdb_appender channels_appender = nullptr;
		if (duckdb_appender_create(conn, nullptr, "channels", &channels_appender) == DuckDBError) {
			throw std::runtime_error("Не удалось создать DuckDB appender для channels таблицы");
		}

		for (int ch = 0; ch < header.num_channels; ch++) {
			duckdb_appender_begin_row(channels_appender);
			duckdb_append_varchar(channels_appender, header.channel_names[ch].c_str());
			duckdb_append_double(channels_appender,
			                     header.lsbs.empty() ? header.lsb_default : header.lsbs[ch]);
			duckdb_append_varchar(channels_appender,
			                      header.units.empty() ? "" : header.units[ch].c_str());
			duckdb_appender_end_row(channels_appender);
		}

		if (duckdb_appender_flush(channels_appender) == DuckDBError) {
			duckdb_error_data error = duckdb_appender_error_data(channels_appender);
			std::string error_message = duckdb_error_data_message(error);
			duckdb_destroy_error_data(&error);
			duckdb_appender_destroy(&channels_appender);
			throw std::runtime_error(
			    std::format("Не удалось записать метаданные каналов в DuckDB: {}", error_message));
		}

		duckdb_appender_destroy(&channels_appender);
	}

	static std::string create_select_query_for_signal_data(const SignalData &data) {
		std::string sql_query = "SELECT ";
		for (int ch = 0; ch < data.num_channels; ch++) {
			if (ch > 0) {
				sql_query += ", ";
			}

			std::string channel_name =
			    !data.channel_names[ch].empty() ? data.channel_names[ch] : std::format("Ch {}", ch);
			sql_query += channel_name;
		}

		sql_query += " FROM points WHERE id >= ? AND id <= ?;";
		return sql_query;
	}

	static void process_column(duckdb_vector vec, int32_t *target, size_t chunk_size) {
		void *data_ptr = duckdb_vector_get_data(vec);
		uint64_t *validity = duckdb_vector_get_validity(vec);
		const auto *source = static_cast<int32_t *>(data_ptr);

		if (validity == nullptr) {
			std::copy(source, source + chunk_size, target);
			return;
		}

		for (size_t row = 0; row < chunk_size; row++) {
			target[row] = duckdb_validity_row_is_valid(validity, row) ? source[row] : 0;
		}
	}

	static void transfer_chunk_to_channel_data(duckdb_data_chunk &chunk, duckdb_result &res,
	                                           signals::SignalData &data,
	                                           duckdb_prepared_statement &stmt, int64_t &offset,
	                                           const int64_t points_to_read) {
		while ((chunk = duckdb_stream_fetch_chunk(res)) != nullptr) {
			size_t chunk_size = duckdb_data_chunk_get_size(chunk);
			size_t num_columns = duckdb_data_chunk_get_column_count(chunk);

			if (const auto expected_columns = static_cast<decltype(num_columns)>(data.num_channels);
			    num_columns != expected_columns) {
				duckdb_destroy_data_chunk(&chunk);
				duckdb_destroy_result(&res);
				duckdb_destroy_prepare(&stmt);
				throw std::runtime_error("Количество столбцов в данных не соответствует количеству "
				                         "каналов в метаданных");
			}

			if (offset + static_cast<int64_t>(chunk_size) > points_to_read) {
				duckdb_destroy_data_chunk(&chunk);
				duckdb_destroy_result(&res);
				duckdb_destroy_prepare(&stmt);
				throw std::runtime_error(
				    "Получен блок данных, выходящий за пределы запрошенного диапазона точек");
			}

			for (size_t col = 0; col < num_columns; col++) {
				duckdb_vector vec = duckdb_data_chunk_get_vector(chunk, col);

				int32_t *target = data.raw_channel(static_cast<int>(col)) + offset;
				process_column(vec, target, chunk_size);
			}

			offset += static_cast<int64_t>(chunk_size);
			duckdb_destroy_data_chunk(&chunk);
		}
	}

	static void append_points_block(duckdb_appender points_appender,
	                                const std::vector<int32_t> &chunk_buffer,
	                                int64_t points_in_block, int32_t num_channels,
	                                int64_t first_point_id_in_block) {
		const auto channels = static_cast<size_t>(num_channels);
		for (int64_t point = 0; point < points_in_block; point++) {
			duckdb_appender_begin_row(points_appender);
			duckdb_append_int64(points_appender, first_point_id_in_block + point);

			const size_t base = static_cast<size_t>(point) * channels;
			for (size_t ch = 0; ch < channels; ch++) {
				duckdb_append_int32(points_appender, chunk_buffer[base + ch]);
			}

			duckdb_appender_end_row(points_appender);
		}
	}

	static void duckdb_read_signal_metadata(duckdb_connection conn, SignalHeader &metadata) {
		metadata = SignalHeader{};
		duckdb_result res;

		std::string sql_query = "SELECT frequency, lsb_default, \"offset\", time_start, "
		                        "optional_params  FROM signals LIMIT 1;";
		if (duckdb_query(conn, sql_query.c_str(), &res) != DuckDBSuccess) {
			std::string error = duckdb_result_error(&res);
			duckdb_destroy_result(&res);
			throw std::runtime_error(std::format("Не удалось прочитать метаданные: {}", error));
		}
		if (duckdb_row_count(&res) == 0) {
			duckdb_destroy_result(&res);
			throw std::runtime_error("Метаданные не найдены в базе данных");
		}
		metadata.frequency = duckdb_value_double(&res, 0, 0);
		metadata.lsb_default = duckdb_value_double(&res, 1, 0);
		metadata.ifirst_point = duckdb_value_int64(&res, 2, 0);
		char *time_start = duckdb_value_varchar(&res, 3, 0);
		metadata.time_start = time_start ? time_start : "";
		duckdb_free(static_cast<void *>(time_start));
		char *optional_json = duckdb_value_varchar(&res, 4, 0);
		if (optional_json) {
			metadata.optional_fields = parse_json(optional_json);
			duckdb_free(static_cast<void *>(optional_json));
		}
		duckdb_destroy_result(&res);

		sql_query = "SELECT COUNT(*) FROM channels;";
		if (duckdb_query(conn, sql_query.c_str(), &res) != DuckDBSuccess) {
			std::string error = duckdb_result_error(&res);
			duckdb_destroy_result(&res);
			throw std::runtime_error(
			    std::format("Не удалось получить количество каналов: {}", error));
		}

		metadata.num_channels = static_cast<int32_t>(duckdb_value_int64(&res, 0, 0));
		duckdb_destroy_result(&res);

		if (metadata.num_channels == 0) {
			throw std::runtime_error("Количество каналов в базе данных равно нулю");
		}

		sql_query = "SELECT COUNT(*) FROM points;";
		if (duckdb_query(conn, sql_query.c_str(), &res) != DuckDBSuccess) {
			std::string error = duckdb_result_error(&res);
			duckdb_destroy_result(&res);
			throw std::runtime_error(
			    std::format("Не удалось получить количество точек: {}", error));
		}

		int64_t num_points = duckdb_value_int64(&res, 0, 0);
		duckdb_destroy_result(&res);

		if (num_points <= 0) {
			throw std::runtime_error("Точки не найдены в базе данных");
		}

		metadata.ilast_point = metadata.ifirst_point + num_points - 1;

		sql_query = "SELECT name, lsb, unit FROM channels ORDER BY rowid;";
		if (duckdb_query(conn, sql_query.c_str(), &res) != DuckDBSuccess) {
			std::string error = duckdb_result_error(&res);
			duckdb_destroy_result(&res);
			throw std::runtime_error(
			    std::format("Не удалось прочитать метаданные канала: {}", error));
		}
		size_t row_count = duckdb_row_count(&res);
		metadata.channel_names.reserve(row_count);
		metadata.lsbs.reserve(row_count);
		metadata.units.reserve(row_count);
		for (size_t row = 0; row < row_count; row++) {
			char *name = duckdb_value_varchar(&res, 0, row);
			double lsb = duckdb_value_double(&res, 1, row);
			char *unit = duckdb_value_varchar(&res, 2, row);

			metadata.channel_names.emplace_back(name ? name : "");
			metadata.lsbs.push_back(lsb);
			metadata.units.emplace_back(unit ? unit : "");

			duckdb_free(static_cast<void *>(name));
			duckdb_free(static_cast<void *>(unit));
		}
		duckdb_destroy_result(&res);
	}

	static void open_and_set_config(duckdb_config &config, const std::filesystem::path &duckdb_path,
	                                duckdb_database &db, signals::BinReader &reader,
	                                duckdb_connection &conn) {
		if (duckdb_create_config(&config) == DuckDBSuccess) {
			duckdb_set_config(config, "max_memory", "256MB");
			if (duckdb_open_ext(duckdb_path.string().c_str(), &db, config, nullptr) !=
			    DuckDBSuccess) {
				duckdb_destroy_config(&config);
				bin_close(reader);
				throw std::runtime_error(
				    std::format("Не удалось открыть DuckDB: {}", duckdb_path.string()));
			}
			duckdb_destroy_config(&config);
		} else {
			if (duckdb_open(duckdb_path.string().c_str(), &db) != DuckDBSuccess) {
				bin_close(reader);
				throw std::runtime_error(
				    std::format("Не удалось открыть DuckDB: {}", duckdb_path.string()));
			}
		}

		if (duckdb_connect(db, &conn) == DuckDBError) {
			duckdb_close(&db);
			bin_close(reader);
			throw std::runtime_error("Не удалось подключиться к DuckDB");
		}
	}

} // namespace signals