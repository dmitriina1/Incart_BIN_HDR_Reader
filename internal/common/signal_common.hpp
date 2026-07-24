#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

/**
 * @file signal_common.hpp
 * @brief Общие структуры и методы для работы с сигналами
 *
 * Содержит:
 * - @ref SignalHeader - метаданные сигнала (из .hdr)
 * - @ref SignalData - данные и метаданные (данные из .hdr и .bin, данные .bin в double)
 * - методы для нормализации диапазонов
 */
/**
 * @defgroup signal_common Signal Common
 * @brief Общие структуры и методы для работы с сигналами
 */
namespace signals {
	/**
	 * @class SignalHeader
	 * @brief Метаданные сигнала из .hdr
	 */
	class SignalHeader {
	public:
		int32_t num_channels = 0; //< Количество каналов
		double frequency = 0.0; //< Частота дискретизации
		double lsb_default = 1.0; //< lsb по умолчанию
		int64_t ifirst_point = 0; //< Первая точка в файле
		int64_t ilast_point = 0; //< Последняя точка
		std::string time_start; //< Время начала
		std::vector<std::string> channel_names; //< Названия каналов
		std::vector<double> lsbs; //< lsb для каждого канала
		std::vector<std::string> units; //< Единицы измерения для каждого канала

		// S6045 - использование std::map с нестандартным компаратором (std::less<>) для обеспечения
		// порядка ключей в JSON
		std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>>
		    optional_fields;

		/**
		 * @brief Возвращает общее число точек по диапазону [ifirst_point, ilast_point].
		 * @return int64_t Общее количество точек.
		 */
		int64_t total_points() const {
			return ilast_point - ifirst_point + 1;
		}
	};

	/**
	 * @class SignalData
	 * @brief Данные и метаданные (данные из .hdr и .bin, данные в double)
	 */
	class SignalData : public SignalHeader {
	public:
		int64_t num_points = 0; //< Общее количество точек
		std::vector<int32_t>
		    storage; //< общий массив сырых данных [количество каналов x количество точек]

		/**
		 * @brief Возвращает указатель на начало сырых int32 данных канала.
		 */
		int32_t *raw_channel(int ch) {
			return storage.data() + (static_cast<size_t>(ch) * static_cast<size_t>(num_points));
		}

		/**
		 * @brief Возвращает константный указатель на начало сырых int32 данных канала.

		 */
		const int32_t *raw_channel(int ch) const {
			return storage.data() + (static_cast<size_t>(ch) * static_cast<size_t>(num_points));
		}

		/**
		 * @brief Возвращает физическое значение точки канала: raw * lsb.
		 */
		double value(int ch, int64_t point) const {
			const size_t idx = (static_cast<size_t>(ch) * static_cast<size_t>(num_points)) +
			                   static_cast<size_t>(point);
			const double lsb =
			    static_cast<size_t>(ch) < lsbs.size() ? lsbs[static_cast<size_t>(ch)] : lsb_default;
			return static_cast<double>(storage[idx]) * lsb;
		}
	};

	/**
	 * @brief Нормализует диапазон относительно общего числа точек.
	 * @ingroup signal_common
	 *
	 * @param total_points Общее количество точек.
	 * @param point_off Смещение от  диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, берётся доступный диапазон.
	 * @return int64_t Количество точек, которое реально можно прочитать.
	 *
	 * Последовательность работы:
	 * 1. Ограничивает total_points, чтобы нельзя было поставить меньше 0.
	 * 2. Если смещение point_off за пределами диапазона, возвращает 0.
	 * 3. Вычисляет доступный диапазон total_points - point_off.
	 * 4. Возвращает весь диапазон (если point_cnt < 0) или минимум из point_cnt и доступного
	 * диапазона.
	 */
	int64_t normalize_range(int64_t total_points, int64_t point_off, int64_t point_cnt);

	/**
	 * @brief Формирует заголовок для заданного диапазона точек (реальный диапазон).
	 * @ingroup signal_common
	 *
	 * @param header Исходный заголовок сигнала.
	 * @param total_points Общее число
	 * точек.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, берётся доступный диапазон.
	 * @param range_header Выходной заголовок с обновлёнными ifirst/ilast.
	 * @return int64_t
	 * Реальное количество точек в диапазоне.
	 *
	 * Последовательность работы:
	 * 1. Нормализует запрошенный диапазон (с учётом total_points и границ).
	 * 2. Копирует header в range_header.
	 * 3. Пересчитывает range_header.ifirst_point и
	 * range_header.ilast_point.
	 * 4. Возвращает рассчитанное число точек в диапазоне.
	 */
	int64_t make_header_for_range(const SignalHeader &header, int64_t total_points,
	                              int64_t point_off, int64_t point_cnt, SignalHeader &range_header);

	/**
	 * @brief Подготавливает SignalData для чтения диапазона.
	 * @ingroup signal_common
	 *
	 * @param metadata Исходные метаданные сигнала.
	 * @param total_points Общее число точек.
	 * @param point_off Смещение от начала диапазона.
	 * @param point_cnt Количество точек для чтения; если < 0, берётся доступный диапазон.
	 * @param data Выходной SignalData с подготовленным storage.
	 *
	 * @param zero_initialize_storage Если true, storage заполняется нулями.
	 * @return int64_t Реальное количество точек в диапазоне.
	 * @throws std::bad_alloc При ошибке выделения памяти под storage.
	 *
	 * Последовательность работы:
	 * 1. Нормализует запрошенный диапазон (с учётом total_points и границ).
	 * 2. Сбрасывает data и копирует в него заголовок диапазона.
	 * 3.
	 * Записывает num_points.
	 * 4. Если диапазон пустой или каналов нет, завершает работу, в ином случае - выделяет storage
	 * нужного размера.
	 */
	int64_t prepare_signal_data_window(const SignalHeader &metadata, int64_t total_points,
	                                   int64_t point_off, int64_t point_cnt, SignalData &data,
	                                   bool zero_initialize_storage = false);

	/**
	 * @brief Сериализует словарь в JSON-строку.
	 *
	 * @param dict Словарь с ключами-строками и значениями, которые могут быть строками, числами с
	 * плавающей точкой или целыми числами.
	 * @return std::string JSON-представление словаря.
	 */
	std::string serialize_to_json(
	    const std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>> &dict);

	/**
	 * @brief Парсит JSON-строку в словарь.
	 *
	 * @param json JSON-строка, представляющая словарь с ключами-строками и значениями, которые
	 * могут быть строками, числами с плавающей точкой или целыми числами.
	 * @return std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>>
	 * Словарь, полученный из JSON-строки.
	 */
	std::map<std::string, std::variant<std::string, double, int64_t>, std::less<>>
	parse_json(std::string_view json);
} // namespace signals