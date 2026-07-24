#pragma once

#include "../bin/bin_reader.hpp"
#include "../common/signal_common.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Количество строк для предпросмотра в print_first_rows.
 */
extern const int64_t TEST_UTILS_PREVIEW_POINTS;

/**
 * @file test_utils.hpp
 * @brief Вспомогательные методы для тестов, валидации и вывода данных.
 *
 * Содержит методы для:
 * - последовательного и случайного чтения;
 * - проверки корректности чтения данных;
 * - замера времени и вывода статистики.
 */
/**
 * @defgroup test_utils Test Utils
 * @brief Вспомогательные методы для тестов, валидации и вывода данных.
 */
namespace signals {

	// Тестовые параметры сигнала: 8 каналов, 100 точек, 250 Гц, LSB=2.29885
	constexpr int TEST_SIGNAL_CHANNELS = 8;
	constexpr int64_t TEST_SIGNAL_POINTS = 100;
	constexpr double TEST_SIGNAL_FREQUENCY = 250.0;
	constexpr double TEST_SIGNAL_LSB = 2.29885;

	/**
	 * @brief Тест последовательного чтения диапазона чанками.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона (parquet/hdf5/duckdb).
	 * @param source_path Путь к файлу для чтения данных.
	 * @param reference Данные для проверки.
	 * @param chunk_points Размер чанка; при <= 0
	 * используется весь доступный диапазон за один проход.
	 * @return double Время выполнения в секундах.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Нормализует размер чанка относительно signalData.num_points.
	 * 2. Делит диапазон на последовательные чанки.
	 * 3. Для каждого чанка вызывает read_signal(offset, count).
	 * 4. Возвращает суммарное время чтения.
	 */
	double test_serial_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points);

	/**
	 * @brief Тест чтения чанков в случайном порядке.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона (parquet/hdf5/duckdb).
	 * @param source_path Путь к файлу для чтения данных.
	 * @param reference Данные для проверки.
	 * @param chunk_points Размер чанка; при <= 0
	 * используется весь доступный диапазон за один проход.
	 * @return double Время выполнения в секундах.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Нормализует размер чанка и считает число чанков.
	 * 2. Формирует массив индексов чанков и перемешивает его.
	 * 3. Читает чанки в порядке из перемешанного массива.
	 * 4. Возвращает суммарное время чтения.
	 */
	double test_random_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points);

	/**
	 * @brief Проверяет корректность последовательного чтения чанков.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона.
	 * @param source_path Путь к файлу для чтения данных.
	 * @param reference Данные для проверки.
	 * @param chunk_points Размер чанка.
	 * @return true Если все чанки считаны и совпали с исходными.
	 * @return false Если обнаружено несовпадение размеров или значений.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Делит диапазон исходных данных на последовательные чанки.
	 * 2. Для каждого чанка читает данные через read_signal.
	 * 3. Сравнивает канал/точки чанка с соответствующим срезом исходных данных.
	 * 4. При первой ошибке выводит сообщение и возвращает false.
	 */
	bool validate_serial_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points);

	/**
	 * @brief Проверяет корректность чтения чанков в случайном порядке.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона.
	 * @param source_path Путь к файлу для чтения данных.
	 * @param reference Данные для проверки.
	 * @param chunk_points Размер чанка.
	 * @return true Если все случайные чанки совпали с исходными.
	 * @return false Если обнаружено несовпадение размеров или значений.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Формирует и перемешивает порядок индексов чанков.
	 * 2. Читает каждый чанк в этом порядке.
	 * 3. Сверяет полученный чанк с исходным диапазоном.
	 * 4. При первой ошибке возвращает false.
	 */
	bool validate_random_chunk_reading(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &reference,
	    int64_t chunk_points);

	/**
	 * @brief Запускает тест последовательного чтения и выводит статистику.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона.
	 * @param source_path Путь к файлу для чтения данных.
	 * @param original_data Данные для проверки.
	 * @param chunk_points Размер чанка.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Выполняет test_serial_chunk_reading.
	 * 2. Выполняет validate_serial_chunk_reading.
	 * 3. Если валидация успешна, выводит число чанков, время и throughput.
	 * 4. Если исходные данные пустые, выводит пустую статистику.
	 */
	void test_serial_chunk_reading_val(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &original_data,
	    int64_t chunk_points);

	/**
	 * @brief Запускает тест случайного чтения и выводит статистику.
	 * @ingroup test_utils
	 *
	 * @param read_signal Функция чтения диапазона.
	 * @param source_path Путь к файлу для чтения данных.
	 * @param original_data Данные для проверки.
	 * @param chunk_points Размер чанка.
	 * @throws Любые исключения, которые бросает @p read_signal, а также возможные исключения
	 * выделения памяти.
	 *
	 * Последовательность работы:
	 * 1. Выполняет test_random_chunk_reading.
	 * 2. Выполняет validate_random_chunk_reading.
	 * 3. Если валидация успешна, выводит число чанков, время и throughput.
	 * 4. Если исходные данные пустые, выводит пустую статистику.
	 */
	void test_random_chunk_reading_val(
	    const std::function<void(const std::filesystem::path &, SignalData &, int64_t, int64_t)>
	        &read_signal,
	    const std::filesystem::path &source_path, const SignalData &original_data,
	    int64_t chunk_points);

	/**
	 * @brief Измеряет время выполнения произвольной функции.
	 * @ingroup test_utils
	 *
	 * @param fn Функция без аргументов.
	 * @return double Длительность выполнения в секундах.
	 * @throws Любые исключения, которые бросает @p fn.
	 *
	 * Последовательность работы:
	 * 1. Сохраняет время начала.
	 * 2. Вызывает функцию @p fn.
	 * 3. Вычисляет и возвращает потраченное время в секундах.
	 */
	double lead_time(const std::function<void()> &fn);

	/**
	 * @brief Выводит первые и последние строки SignalData.
	 * @ingroup test_utils
	 *
	 * @param data Данные для вывода.
	 * @param preview_points Количество строк в верхнем и нижнем блоке.
	 *
	 * Последовательность работы:
	 * 1. Проверяет, есть ли данные для отображения.
	 * 2. Выводит заголовок таблицы (Row + имена каналов).
	 * 3. Выводит первые @p preview_points строк.
	 * 5. Выводит последние @p preview_points строк.
	 */
	void print_first_rows(const SignalData &data,
	                      int64_t preview_points = TEST_UTILS_PREVIEW_POINTS);

	/**
	 * @brief Сравнивает два набора сигналов по размеру, диапазону и значениям.
	 * @ingroup test_utils
	 *
	 * @param expected Исходные данные (из .bin).
	 * @param actual Сравниваемые данные (из parquet/duckdb/hdf5).
	 * @param label Название источника.
	 * @return true Если данные полностью совпадают.
	 * @return false Если найдено несоответствие.
	 *
	 * Последовательность работы:
	 * 1. Сравнивает num_points и num_channels.
	 * 2. Сравнивает ifirst_point/ilast_point.
	 * 3. Поэлементно сравнивает значения всех каналов.
	 * 4. При ошибке выводит сообщение и возвращает false.
	 */
	bool compare_signal_data(const SignalData &expected, const SignalData &actual,
	                         const char *label);

	/**
	 * @brief Загружает исходные данные из bin через bin_read.
	 * @ingroup test_utils
	 *
	 * @param source_path Путь к .hdr или .bin.
	 * @param point_offset Смещение от
	 * начала диапазона.
	 * @param point_count Количество точек; если < 0, читается доступный диапазон.
	 * @return SignalData Загруженные данные.
	 * @throws std::runtime_error При ошибках открытия/чтения BIN.
	 *
	 * Последовательность работы:
	 * 1. Создаёт временный SignalData.
	 * 2. Вызывает bin_read с переданным диапазоном.
	 * 3. Возвращает заполненную структуру.
	 */
	SignalData load_reference(const std::filesystem::path &source_path, int64_t point_offset = 0,
	                          int64_t point_count = -1);

	/**
	 * @brief Выводит информацию по исходным .bin/.hdr файлам и считанным данным.
	 * @ingroup test_utils
	 *
	 * @param path Путь к .bin/.hdr файлам (расширение игнорируется).
	 * @param data Считанные данные сигнала.
	 * @param read_time Время чтения в секундах.
	 * @throws std::filesystem::filesystem_error Если не удалось получить размер файлов.
	 *
	 * Последовательность работы:
	 * 1. Получает размеры .bin и .hdr через std::filesystem::file_size.
	 * 2. Выводит число каналов, точек, диапазон и частоту.
	 * 3. Выводит размер файлов и время чтения.
	 */
	void print_file_info(const std::filesystem::path &path, const SignalData &data,
	                     double read_time);

	/**
	 * @brief Выводит результаты записи/чтения конвертированного формата.
	 * @ingroup test_utils
	 *
	 * @param format_name Название формата (duckdb/parquet/hdf5 и т.д.).
	 * @param write_time Время записи в секундах.
	 * @param output_size_bytes Размер выходного файла в байтах.
	 * @param read_time Время чтения в секундах.
	 * @param data Прочитанные данные после конвертации.
	 * @param original_total_size Размер исходного набора (.bin и .hdr).
	 *
	 * Последовательность работы:
	 * 1. Выводит размер, время записи и процент сжатия относительно исходного файла.
	 * 2. Выводит информацию по чтению: каналы, точки, диапазон, частота и время.
	 */
	void print_conversion_results(const std::string &format_name, double write_time,
	                              size_t output_size_bytes, double read_time,
	                              const SignalData &data, size_t original_total_size);

	/**
	 * @brief Парсит строку в int64.
	 * @ingroup test_utils
	 *
	 * @param text Входная строка.
	 * @param value Выходное значение при успешном
	 * парсинге.

	 * *
	 * @return true Если строка является корректным целым числом int64.
	 * @return false Если строка пустая/некорректная или вышла за диапазон.
	 *
	 * Последовательность работы:
	 * 1. Проверяет, что строка не пустая.
	 * 2. Обрабатывает знак +/-.
	 * 3. Проверяет, что оставшиеся символы только цифры.
	 * 4. Пытается преобразовать строку через std::stoll.
	 * 5. Возвращает true при успехе, иначе false.
	 */
	bool try_parse_int64(const char *text, int64_t &value);

	/**
	 * @brief Предоставляет базовый путь для тестовых сигналов.
	 *
	 * @param test_name Название теста.
	 * @return std::filesystem::path Путь к тестовым файлам без расширения.
	 */
	std::filesystem::path test_signal_base_path(const char *test_name);

	/**
	 * @brief Создает и заполняет тестовые .hdr и .bin файлы для заданного базового пути.
	 *
	 * @param base_path Базовый путь для файлов (без расширения).
	 */
	void create_test_signal_files(const std::filesystem::path &base_path);

	/**
	 * @brief Проверяет корректность заголовочного файла (hdr) тестового сигнала.
	 *
	 * @param header Заголовок для проверки.
	 * @return true Если заголовок
	 * соответствует ожидаемым параметрам тестового сигнала.
	 * @return false Если заголовок не соответствует ожидаемым параметрам.
	 */
	bool validate_test_signal_header(const SignalHeader &header);

	/**
	 * @brief Проверяет корректность данных тестового сигнала.
	 *
	 * @param data Считанные данные для проверки.
	 * @param first_point Начальная точка диапазона для проверки.
	 * @param point_count Количество точек для проверки; если < 0, проверяется весь доступный
	 * диапазон.
	 * @return true Если данные соответствуют ожидаемым значениям тестового сигнала в заданном
	 * диапазоне.
	 * @return false Если данные не соответствуют ожидаемым значениям или произошла ошибка
	 * валидации.
	 */
	bool validate_test_signal_data(const SignalData &data, int64_t first_point,
	                               int64_t point_count);
} // namespace signals