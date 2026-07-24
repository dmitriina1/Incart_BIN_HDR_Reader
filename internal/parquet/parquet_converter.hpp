#pragma once

#include "../bin/bin_reader.hpp"
#include "../common/signal_common.hpp"

#include <algorithm>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <filesystem>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file parquet_converter.hpp
 * @brief Запись и чтение сигналов в формате Parquet.
 */
/**
 * @defgroup parquet_converter Parquet Converter
 * @brief Запись и чтение сигналов в формате Parquet.
 */
namespace signals {
	/**
	 * @struct ParquetReader
	 * @brief Состояние открытого Parquet reader-а.
	 * @ingroup parquet_converter
	 *
	 * Хранит открытый parquet-файл, объект Parquet reader-а, LRU-кэш row groups
	 * и индекс смещений row groups для ускорения random-доступа по диапазонам.
	 */
	struct ParquetReader {
		std::shared_ptr<arrow::io::ReadableFile> file; //< Открытый parquet-файл.
		std::unique_ptr<parquet::arrow::FileReader>
		    reader; //< Reader для чтения row groups и колонок.
		std::unordered_map<int, std::shared_ptr<arrow::Table>>
		    row_group_cache; //< Кэш row group: индекс -> таблица.
		std::vector<int>
		    row_group_cache_order; //< Порядок LRU для управления размером row_group_cache.
		std::vector<int64_t> row_group_first_row; //< Первая строка каждой row group.
		std::vector<int64_t> row_group_row_count; //< Количество строк в каждой row group.
		int num_row_groups = 0; //< Общее количество row groups в файле.
	};

	/**
	 * @brief Открывает Parquet-файл и инициализирует кэш row groups.
	 * @ingroup parquet_converter
	 *
	 * @param path Путь к Parquet-файлу.
	 * @param reader Выходная структура ParquetReader.
	 * @return arrow::Status Статус выполнения
	 * (OK или ошибка Parquet).
	 *
	 * Последовательность работы:
	 * 1. Сбрасывает reader и очищает кэш row groups.
	 * 2. Открывает файл через Arrow I/O.
	 * 3. Создаёт parquet::arrow::FileReader.
	 * 4. Включает многопоточность и сохраняет число row groups.
	 * 5. Строит индекс row_group_first_row / row_group_row_count для быстрого поиска пересечений
	 * диапазона с row groups.
	 */
	arrow::Status parquet_open(const std::filesystem::path &path, ParquetReader &reader);

	/**
	 * @brief Закрывает Parquet reader и очищает кэш row groups.
	 * @ingroup parquet_converter
	 *
	 * @param reader Состояние reader-а.
	 *
	 * Последовательность работы:
	 * 1. Очищает кэш таблиц row groups и порядок LRU.
	 * 2. Очищает индекс row_group_first_row / row_group_row_count.
	 * 3. Сбрасывает объекты reader/file.
	 * 4. Обнуляет num_row_groups.
	 */
	void parquet_close(ParquetReader &reader);

	/**
	 * @brief Записывает диапазон точек из .bin в Parquet.
	 * @ingroup parquet_converter
	 *
	 * @param bin_path Путь к .hdr/.bin файлу.
	 * @param parquet_path Путь к выходному Parquet
	 * файлу.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек; если < 0, пишется доступный диапазон.
	 * @param row_group_points Размер row group.
	 * @return arrow::Status Статус выполнения (OK или ошибка Parquet).
	 *
	 * Последовательность работы:
	 * 1. Проверяет входные параметры и открывает bin-reader.
	 * 2. Нормализует диапазон и строит схему полей ch0..chN-1.
	 * 3. Создаёт Parquet writer и настраивает max_row_group_length = row_group_points.
	 * 4. Пишет общие метаданные в file key-value metadata, включая массивы каналов
	 *    (channel_name.N/lsb.N/unit.N), а также поканальные metadata у полей схемы.
	 * 5. Читает .bin чанками, помещает данные по каналам.
	 * 6. Формирует RecordBatch и записывает его в Parquet.
	 * 7. Закрывает writer и возвращает итоговый Status.
	 */
	arrow::Status parquet_write_signals_data(const std::filesystem::path &bin_path,
	                                         const std::filesystem::path &parquet_path,
	                                         int64_t point_off, int64_t point_cnt,
	                                         int64_t row_group_points = 100000);

	/**
	 * @brief Потоково читает диапазон точек через ParquetReader.
	 * @ingroup parquet_converter
	 *
	 * @param reader Открытый ParquetReader.
	 * @param data Выходная структура SignalData.
	 *
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек; если < 0, читается доступный диапазон.
	 * @return arrow::Status Статус выполнения (OK или ошибка).
	 * @throws std::invalid_argument Возможен при некорректных числовых значениях в metadata.
	 * @throws std::out_of_range Возможен при некорректных числовых значениях в metadata.
	 * @throws std::runtime_error При ошибках доступа к metadata поля.
	 *
	 * Последовательность работы:
	 * 1. Проверяет наличие row groups и metadata.
	 * 2. Восстанавливает SignalHeader из file key-value metadata и схемы Parquet.
	 * 3. Подготавливает окно data через prepare_signal_data_window.
	 * 4. По
	 * индексу row_group_first_row находит первую пересекающуюся row group.
	 * 5. Итерируется только по пересекающимся row groups.
	 * 6. Читает из кэша нужную row group, копирует нужный фрагмент.
	 * 7. Конвертирует int32 в double с учётом LSB и заполняет data.

	 */
	arrow::Status parquet_read_signal_data_like_stream(ParquetReader &reader, SignalData &data,
	                                                   int64_t point_off, int64_t point_cnt);

	/**
	 * @brief Потоково читает диапазон точек через filepath.
	 * @ingroup parquet_converter
	 *
	 * @param parquet_path Путь к Parquet-файлу.
	 * @param data Выходная структура SignalData.
	 * @param point_off Смещение от
	 * начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается доступный хвост.
	 * @throws std::runtime_error Если не удалось открыть файл, чтение завершилось ошибкой Status
	 * или metadata поля недоступны.
	 * @throws std::invalid_argument Возможен при некорректных числовых значениях в metadata.
	 * @throws std::out_of_range Возможен при некорректных числовых значениях в metadata.
	 *
	 * Последовательность работы:
	 * 1. Использует кэшированный ParquetReader для повторных вызовов.
	 * 2. При смене файла переоткрывает reader.
	 * 3. Вызывает parquet_read_signal_data_like_stream(ParquetReader&, ...).
	 * 4. Преобразует не-OK Status в исключение std::runtime_error.
	 */
	void parquet_read_signal_data_like_stream(const std::filesystem::path &parquet_path,
	                                          SignalData &data, int64_t point_off,
	                                          int64_t point_cnt);

	/**
	 * @brief Обёртка над parquet_write_signals_data с исключением при ошибке.
	 * @ingroup parquet_converter
	 *
	 * @param bin_path Путь к .hdr/.bin файлу.
	 * @param parquet_path Путь к выходному
	 * Parquet-файлу.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек; если < 0, пишется доступный диапазона.
	 * @param row_group_points Размер row group.
	 * @throws std::runtime_error Если parquet_write_signals_data вернул ошибку.
	 *
	 * Последовательность работы:
	 * 1. Вызывает parquet_write_signals_data.
	 * 2. Проверяет возвращённый Status.
	 * 3. В случае ошибки бросает std::runtime_error с текстом Status.
	 */
	void parquet_write_signal_data_like_stream(const std::filesystem::path &bin_path,
	                                           const std::filesystem::path &parquet_path,
	                                           int64_t point_off, int64_t point_cnt,
	                                           int64_t row_group_points = 100000);
} // namespace signals