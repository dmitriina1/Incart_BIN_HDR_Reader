#pragma once

#include "../bin/bin_reader.hpp"
#include "../common/signal_common.hpp"
#include "../hdr/hdr_reader.hpp"

#include <H5Cpp.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @file hdf5_converter.hpp
 * @brief Запись и потоковое чтение сигналов в формате HDF5.
 */
/**
 * @defgroup hdf5_converter HDF5 Converter
 * @brief Запись и потоковое чтение сигналов в формате HDF5.
 */
namespace signals {
	/**
	 * @brief Открывает HDF5-файл для потокового чтения.
	 * @ingroup hdf5_converter
	 *
	 * @param hdf5_path Путь к HDF5-файлу.
	 * @return void* handle для дальнейших операций.
	 * @throws H5::Exception При ошибке открытия файла или инициализации HDF5.
	 *
	 * Последовательность работы:
	 * 1. Формирует параметры доступа и кэша HDF5.
	 * 2. Создаёт объект H5::H5File в режиме чтения.
	 * 3. Возвращает указатель как handle.
	 */
	void *hdf5_open(const std::filesystem::path &hdf5_path);

	/**
	 * @brief Закрывает ранее открытый HDF5-handle.
	 * @ingroup hdf5_converter
	 *
	 * @param reader_handle Ссылка на handle, после закрытия обнуляется.
	 * @throws H5::Exception При ошибке закрытия файла.
	 *
	 * Последовательность работы:
	 * 1. Если handle пустой, завершает работу без действий.
	 * 2. Приводит handle к H5::H5File*.
	 * 3. Закрывает файл и освобождает объект.
	 * 4. Устанавливает handle в nullptr.
	 */
	void hdf5_close(void *&reader_handle);

	/**
	 * @brief Потоково читает диапазон точек через ранее открытый handle.
	 * @ingroup hdf5_converter
	 *
	 * @param reader_handle handle, полученный из hdf5_open.
	 * @param data Выходная структура SignalData.
	 * @param point_off Смещение от
	 * начала диапазона.
	 * @param point_cnt Количество точек; если < 0, читается доступный диапазон.
	 * @param chunk_points Размер чанка чтения.
	 * @throws std::logic_error Если handle не открыт.
	 * @throws std::invalid_argument Если параметры диапазона/чанка некорректны.
	 * @throws std::out_of_range Если запрошенный диапазон больше доступного.
	 * @throws H5::Exception При ошибках операций HDF5.
	 *
	 * Последовательность работы:
	 * 1. Проверяет и извлекает H5::H5File из handle.
	 * 2. Читает через метод hdf5_read_signal_data_like_stream с H5::H5File&.
	 */
	void hdf5_read_signal_data_like_stream(void *reader_handle, SignalData &data, int64_t point_off,
	                                       int64_t point_cnt, int64_t chunk_points);

	/**
	 * @brief Конвертирует диапазон точек из .bin в HDF5.
	 * @ingroup hdf5_converter
	 *
	 * @param bin_path Путь к .hdr/.bin файлу.
	 * @param hdf5_path Путь к выходному
	 * HDF5-файлу.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, пишется доступный диапазон.
	 * @throws std::runtime_error При ошибках чтения BIN или неполной записи.
	 * @throws H5::Exception При ошибках создания групп/датасетов/атрибутов HDF5.
	 *
	 * Последовательность работы:
	 * 1. Открывает bin-reader и нормализует диапазон.
	 * 2. Создаёт/перезаписывает HDF5-файл.
	 * 3. Записывает metadata в /signal_header_data.
	 * 4. Записывает channel_names/lsbs/units в /signal_data.
	 * 5. Читает bin чанками и записывает raw int32 в dataset data.
	 */
	void hdf5_write_signals_data(const std::filesystem::path &bin_path,
	                             const std::filesystem::path &hdf5_path, int64_t point_off,
	                             int64_t point_cnt);

	/**
	 * @brief Потоково читает диапазон точек из открытого HDF5-файла через H5::H5File.
	 * @ingroup hdf5_converter
	 *
	 * @param file Открытый объект H5::H5File.
	 * @param data Выходная структура SignalData.
	 * @param point_off Смещение от
	 * начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается доступный диапазон.
	 * @param chunk_points Размер чанка чтения.
	 * @throws std::invalid_argument Если point_off < 0, chunk_points <= 0 или обнаружен нулевой
	 * LSB.
	 * @throws std::out_of_range Если диапазон выходит за границы данных файла.
	 * @throws H5::Exception При ошибках чтения структур HDF5.
	 *
	 * Последовательность работы:
	 * 1. Валидирует входные параметры.
	 * 2. Считывает metadata и данные каналов из HDF5.
	 * 3. Подготавливает data и буферы для чанков.
	 * 4. Циклом
	 * читает блоки int32.
	 * 5. Конвертирует каждый блок в double с учётом LSB и пишет в SignalData.
	 */
	void hdf5_read_signal_data_like_stream(const H5::H5File &file, SignalData &data,
	                                       int64_t point_off, int64_t point_cnt,
	                                       int64_t chunk_points);

	/**
	 * @brief Потоково читает диапазон точек из HDF5 через filepath.
	 * @ingroup hdf5_converter
	 *
	 * @param hdf5_path Путь к HDF5-файлу.
	 * @param data Выходная структура SignalData.
	 * @param point_off Смещение от
	 * начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, читается доступный хвост.
	 * @param chunk_points Размер чанка.
	 * @throws std::logic_error Если handle не открыт.
	 * @throws std::invalid_argument Если параметры диапазона/чанка некорректны.
	 * @throws std::out_of_range Если диапазон выходит за границы данных файла.
	 * @throws H5::Exception При ошибках открытия/чтения HDF5.
	 *
	 * Последовательность работы:
	 * 1. Сравнивает запрошенный путь с кэшированным.
	 * 2. При смене файла закрывает старый handle и открывает новый.
	 * 3. Читает через метод hdf5_read_signal_data_like_stream с handle.
	 */
	void hdf5_read_signal_data_like_stream(const std::filesystem::path &hdf5_path, SignalData &data,
	                                       int64_t point_off, int64_t point_cnt,
	                                       int64_t chunk_points);
} // namespace signals