#pragma once

#include "../bin/bin_reader.hpp"
#include "../common/signal_common.hpp"
#include "cmath"
#include <duckdb.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

/**
 * @file duckdb_converter.hpp
 * @brief Запись и чтение сигналов в формате Duckdb.
 */
/**
 * @defgroup duckdb_converter DuckDB Converter
 * @brief Запись и чтение сигналов в формате Duckdb.
 */
namespace signals {

	// Дефолтные параметры для записи в DuckDB
	static constexpr int64_t DEFAULT_DUCKDB_CHUNK_POINTS = 100000;
	static constexpr int64_t DEFAULT_DUCKDB_COMMIT_POINTS = 500000;

	/**
	 * @brief Создаёт схему таблиц DuckDB для хранения сигнала.
	 * @ingroup duckdb_converter
	 *
	 * @param conn Открытое DuckDB-соединение.
	 * @param header Метаданные сигнала для динамического построения таблицы points.

	 * *
	 * @throws std::runtime_error Если не удалось создать любую из таблиц схемы.
	 *
	 * Последовательность работы:
	 * 1. Создаёт таблицу метаданных signals.
	 * 2. Создаёт таблицу каналов channels.
	 * 3. Создаёт таблицу отсчётов points с колонками Ch0..ChN-1.
	 */
	void create_db_schema(duckdb_connection conn, SignalHeader &header);

	/**
	 * @brief Потоково читает диапазон точек из DuckDB через соединение.
	 * @ingroup duckdb_converter
	 *
	 * @param conn Открытое DuckDB-соединение.
	 * @param data Выходная структура SignalData.
	 * @param point_off Смещение от
	 * начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается доступный диапазон.
	 * @throws std::invalid_argument Если point_off отрицательный.
	 * @throws std::runtime_error При ошибках SQL, несогласованности схемы, несовпадении объёма
	 * чтения или недопустимом LSB.
	 *
	 * Последовательность работы:
	 * 1. Валидирует входные параметры.
	 * 2. Читает метаданные и подготавливает окно чтения.
	 * 3. Готовит SELECT по id BETWEEN ? AND ?.
	 * 4. Потоково читает chunks и конвертирует int32 в double.
	 * 5. Проверяет, что прочитано ровно ожидаемое число точек.
	 */
	void duckdb_read_signal_data_like_stream(const std::filesystem::path &duckdb_path,
	                                         SignalData &data, int64_t point_off,
	                                         int64_t point_cnt);

	/**
	 * @brief Потоково записывает диапазон точек в DuckDB через filepath с периодическими commit.
	 * @ingroup duckdb_converter
	 *
	 * @param bin_path Путь к .hdr/.bin источнику.
	 * @param duckdb_path Путь к DuckDB-файлу назначения.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается доступный диапазон.
	 * @param chunk_points Размер чтения/записи чанка в точках.
	 * @param commit_every_n_points Количество точек между промежуточными commit.
	 * @throws std::invalid_argument Если chunk_points <= 0 или point_off < 0.
	 * @throws std::runtime_error При ошибках чтения BIN, транзакций, appender-а или подключения
	 * DuckDB.
	 *
	 * Последовательность работы:
	 * 1. Открывает .bin и DuckDB (с попыткой задать лимит памяти).
	 * 2. Создаёт схему и записывает метаданные сигнала.
	 * 3. Запускает транзакцию и appender для таблицы points.
	 * 4. Читает BIN чанками и вставляет строки.
	 * 5. При достижении порога commit_every_n_points делает commit и открывает новую транзакцию.
	 * 6. Выполняет финальный commit и закрывает ресурсы.
	 */
	void duckdb_write_signal_data_like_stream(
	    const std::filesystem::path &bin_path, const std::filesystem::path &duckdb_path,
	    int64_t point_off, int64_t point_cnt, int64_t chunk_points = DEFAULT_DUCKDB_CHUNK_POINTS,
	    int64_t commit_every_n_points = DEFAULT_DUCKDB_COMMIT_POINTS);

} // namespace signals