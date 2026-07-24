#pragma once

#include "../common/signal_common.hpp"

#include <filesystem>
#include <fstream>

/**
 * @file bin_reader.hpp
 * @brief Чтение сигналов из файлов .hdr/.bin.
 *
 * Предоставляет потоковое чтение int32-данных из бинарного файла.
 */
namespace signals {
	/**
	 * @struct BinReader
	 * @brief Состояние открытого бинарного reader-а.
	 *
	 * Хранит метаданные сигнала, путь к .bin, поток файла и текущую позицию чтения.
	 */
	struct BinReader {
		SignalHeader header; //< Данные из .hdr
		std::filesystem::path bin_path; //< Путь к .bin
		std::ifstream file; //< Поток данных
		int64_t total_points = 0; //< Общее количество точек
		int64_t current_point = 0; //< Текущая позиция при чтении
	};

	/**
	 * @brief Открывает reader для работы с .bin и заполняет метаданные.
	 *
	 * @param bin_path Путь к .hdr или .bin (путь к другому файлу определяется автоматически в той
	 * же
	 * директории).
	 * @param reader reader, который будет полностью инициализирован.
	 * @throws std::system_error Если не удалось открыть .bin.
	 * @throws std::runtime_error Если не удалось прочитать .hdr или в заголовке указано
	 * некорректное количество каналов.
	 *
	 * Последовательность работы:
	 * 1. Сбрасывает текущее состояние @p reader.
	 * 2. Строит базовое имя и пути к .hdr и .bin.
	 * 3. Читает метаданные из .hdr.
	 * 4. Открывает бинарный поток .bin.
	 * 5. Проверяет валидность числа каналов и вычисляет total_points.
	 */
	void bin_open(const std::filesystem::path &bin_path, BinReader &reader);

	/**
	 * @brief Закрывает reader и очищает его.
	 *
	 * @param reader Открытый или закрытый reader.
	 *
	 * Последовательность работы:
	 * 1. Если поток файла открыт, выполняет close().
	 * 2. Сбрасывает все поля структуры к значениям по умолчанию.
	 */
	void bin_close(BinReader &reader);

	/**
	 * @brief Перемещает позицию чтения на заданную позицию по смещению.
	 *
	 * @param reader Открытый reader.
	 * @param point_off Смещение от начала диапазона.
	 * @throws std::logic_error Если reader не открыт.
	 * @throws std::invalid_argument Если смещение отрицательное.
	 * @throws std::runtime_error Если не удалось выполнить seek.
	 *
	 * Последовательность работы:
	 * 1. Проверяет, что файл открыт и смещение неотрицательное.
	 * 2. Максимальное смещение ограничено значением total_points.
	 * 3. Пересчитывает смещение в байты: point_off * (num_channels * sizeof(int32_t)).
	 * 4. Выполняет seekg - смещение на bytes_offset байт.
	 * 5. Обновляет current_point.
	 */
	void bin_seek(BinReader &reader, int64_t point_off);

	/**
	 * @brief Читает чанк сырых int32-данных.
	 *
	 * @param reader Открытый reader.
	 * @param point_cnt Количество точек для чтения.
	 * @param block_buf Буфер, который будет заполнен значениями в формате [point0_ch0, point1_ch0,
	 * point0_ch1, ..., pointN_chM].
	 * @return int64_t Прочитанное количество точек.
	 * @throws std::logic_error Если reader не открыт.
	 * @throws std::runtime_error Если произошла ошибка чтения бинарного потока.
	 *
	 * Последовательность работы:
	 * 1. Проверяет состояние reader.
	 * 2. Нормализует запрошенный диапазон через normalize_range.
	 * 3. Изменяет размер буфера под points_to_read * num_channels.
	 * 4. Читает блок из файла в буфер.
	 * 5. Сдвигает current_point на количество прочитанных точек.
	 */
	int64_t bin_read_data_chunk(BinReader &reader, int64_t point_cnt,
	                            std::vector<int32_t> &block_buf);

	/**
	 * @brief Полностью читает диапазон точек из bin в один SignalData.
	 *
	 * @param bin_path Путь к .hdr или .bin.
	 * @param point_off Смещение от начала
	 * диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается весь доступный диапазон.
	 * @param data Выходная структура с данными диапазона.
	 * @throws std::invalid_argument При некорректных параметрах диапазона.
	 * @throws std::logic_error Если reader оказался в некорректном состоянии.
	 * @throws std::system_error При ошибке открытия файла или выделения памяти.
	 * @throws std::runtime_error При ошибке чтения, позиционирования или конвертации данных.
	 *
	 * Последовательность работы:
	 * 1. Открывает reader через bin_open.
	 * 2. Нормализует диапазон чтения и выполняет bin_seek.
	 * 3. Инициализирует выходной SignalData и выделяет итоговый буфер.
	 * 4. В цикле читает данные чанками фиксированного размера.
	 * 5. Для каждого чанка размещает сырые значения по каналам.
	 */
	void bin_read(const std::filesystem::path &bin_path, int64_t point_off, int64_t point_cnt,
	              SignalData &data);
} // namespace signals