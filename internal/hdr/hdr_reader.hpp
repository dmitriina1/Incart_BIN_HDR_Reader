#pragma once

#include "../common/signal_common.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

/**
 * @file hdr_reader.hpp
 * @brief Чтение метаданных сигнала из файла .hdr.
 *
 * Извлекает параметры сигнала (каналы, диапазон точек, LSB, единицы
 * измерения и т.д.) и заполняет структуру SignalHeader.
 */
namespace signals {
	/**
	 * @brief Читает .hdr и заполняет структуру метаданных сигнала.
	 *
	 * @param bin_path Путь к .hdr или .bin (путь к файлу определяется автоматически в той же
	 *
	 * директории, если указан .bin).
	 * @param header Выходная структура SignalHeader.
	 *
	 * @throws std::system_error Если файл не открылся.
	 * @throws std::runtime_error Если формат файла некорректен/неполон.
	 *
	 * Последовательность работы:
	 * 1. Преобразует входной путь к пути .hdr.
	 * 2. Открывает файл заголовка в бинарном режиме.
	 * 3. Пропускает UTF-8 BOM (если присутствует).
	 * 4. Сбрасывает @p header к значениям по умолчанию.
	 * 5. Парсит первую строку: число
	 * каналов, частоту дискретизации, lsb_default.
	 * 6. Парсит диапазон точек и время старта.
	 * 7. Читает названия каналов.
	 * 8. Читает LSB для каждого канала.
	 * 9. Читает единицы измерения для каждого канала.
	 */
	void hdr_read(const std::filesystem::path &bin_path, SignalHeader &header);
} // namespace signals