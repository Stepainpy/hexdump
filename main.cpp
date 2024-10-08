/* ========================================

Зависимости: C++ 20 и библиотека argh

Это простая программа для вывода
шестнадцатеричного содержания файла в
консоль. Для дополнительной информации
введите "hexdump help".

Для g++ 14.1 и флагов -O3 -s размер
программы будет ровно 64 КБ.

======================================== */

#include <iostream>
#include <fstream>
#include <string>

// Парсер аргументов командной строки
// https://github.com/adishavit/argh
#include "argh.h"

using namespace std::literals;
using params_t = std::initializer_list<const char* const>;

/* ========================================
            Глобальные константы
======================================== */

constexpr size_t page_row_count = 32;
// U+00B7 middle dot, 0xfa для CP866
constexpr char dot_char = '\xfa';

namespace ansi_color {
    constexpr auto red  = "31"sv;
    constexpr auto blue = "34"sv;
    constexpr auto mgnt = "35"sv;
    constexpr auto cyan = "36"sv;
    constexpr auto grey = "38;5;243"sv;

    constexpr auto utf8_blue = "38;5;12"sv;               // для 1 байта
    constexpr auto utf8_gren      = "38;2;43;176;23"sv;   // для 2 байтов
    constexpr auto utf8_pale_gren = "38;2;99;220;100"sv;
    constexpr auto utf8_yllw      = "38;2;228;228;35"sv;  // для 3 байт
    constexpr auto utf8_pale_yllw = "38;2;229;217;108"sv;
    constexpr auto utf8_red       = "38;2;210;25;25"sv;   // для 4 байт
    constexpr auto utf8_pale_red  = "38;2;209;121;121"sv;
}

/* ========================================
            Строковые константы
======================================== */

constexpr auto base62_digits =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"sv;

namespace hex_digits {
    constexpr auto lower = "0123456789abcdef"sv;
    constexpr auto upper = "0123456789ABCDEF"sv;
}

namespace message_prefix {
    constexpr auto info  = "[\x1b[34mINFO\x1b[0m] "sv;  // Синий
    constexpr auto error = "[\x1b[31mERROR\x1b[0m] "sv; // Красный
}

constexpr auto help_message = R"(usage:
  hexdump <file> [-b <size>] [-c <size>] [-n] [-u] [-z]
  hexdump help
where:
  -b, --bytes-in-row   Count of bytes in row.
  -c, --column-width   Count of bytes between a double space separator.
  -n, --without-text   Disable output of interpretated text.
  -u, --colored-utf8   Prints bytes with color in accordance with UTF-8 encoding.
  -z, --pallid-zeros   Prints zeros in grey. Not work with -u.
)"sv;

namespace arg {
    constexpr params_t
        bytes = {"-b", "--bytes-in-row"},
        width = {"-c", "--colunm-width"},
        ascii = {"-n", "--without-text"},
        color = {"-u", "--colored-utf8"},
        zeros = {"-z", "--pallid-zeros"};
}

/* ========================================
                  Функции
======================================== */

size_t get_hex_digit_count(size_t number) {
    if (!number) return 1;
    size_t count = 0;
    while (number) { number >>= 4; ++count; }
    return count;
}

bool is_any_whitespace(char ch) {
    return std::isspace(ch) || ch == '\b' || ch == '\x1b' ||
        ch == '\xff' || ch == '\x7f';
}

char whitespace_to_char(char ch) {
    switch (ch) {
        case '\n': return 'n'; // перенос строки
        case '\r': return 'r'; // возврат каретки
        case '\t': return 't'; // табуляция
        case '\b': return 'b'; // backspace
        case '\v': return 'v'; // вертикальная табуляция
        case '\f': return 'f'; // form feed
        case  127: return 'D'; // удаление
        case   27: return 'e'; // escape
        case   -1: return '#'; // 255 или FF
        default  : return '?'; // всё остальное неизвестное
    }
}

std::string fixed_width_hex_number(size_t number, size_t width, bool upper = false) {
    std::string result(width, '0');
    for (size_t i = width - 1, j = 0; i < width; i--, j += 4)
        result[i] = (upper ? hex_digits::upper : hex_digits::lower)[(number >> j) & 15];
    return result;
}

std::string set_color(const std::string_view color, const std::string_view input) {
    return "\x1b["s + color.data() + 'm' + input.data() + "\x1b[0m"s;
}

std::string set_color(const std::string_view color, char input) {
    return "\x1b["s + color.data() + 'm' + input + "\x1b[0m"s;
}

// Генерирует строку заголовка c отступами
std::string generate_header(size_t byte_count, size_t column_width, bool with_text_column) {
    std::string result;

    for (size_t i = 0; i < byte_count; i++) // Смещения для байтов
        result += fixed_width_hex_number(i, 2, true) +
            (i % column_width == column_width - 1 ? "  " : " ");

    if (with_text_column) {
        if (result[result.size() - 2] != ' ') result += ' '; // Разделитель

        for (size_t i = 0; i < byte_count; i++) { // Смещения для символов
            result += base62_digits[i % base62_digits.size()];
            if (i % column_width == column_width - 1) result += ' ';
        }
    }
    
    return result;
}

void extract_parameter(
    const argh::parser& psr, params_t params,
    size_t& output_var, size_t default_value)
{
    if (!(psr(params, default_value) >> output_var)) {
        std::cerr << message_prefix::error <<
        "Must provide a valid value. Got '"sv
        << psr(params).str() << "'\n"sv;
        std::exit(EXIT_FAILURE);
    }
}

void info_out_of_range(bool inequality, size_t max,
    size_t got_value, const std::string_view param_name)
{
    if (inequality) {
        std::cout << message_prefix::info <<
        "Going beyond the range [1; "sv << max <<
        "] with a value of "sv << got_value <<
        " for parameter '"sv << param_name << "'\n"sv;
    }
}

/* ========================================
              Начало программы
======================================== */

int main(int argc, char** argv) {
    // Удаляет аргумент с именем программы
    --argc; ++argv;
    // Проверка аргументов
    if (!argc) {
        std::cerr << message_prefix::error << "Not enough arguments\n"sv;
        std::cerr << message_prefix::info << "Try enter 'hexdump help'\n"sv;
        return EXIT_FAILURE;
    } else if (argc == 1 && argv[0] == "help"sv) {
        std::cout << help_message;
        return EXIT_SUCCESS;
    }

    // Парсинг аргументов
    argh::parser parser;
    parser.add_params(arg::bytes);
    parser.add_params(arg::width);
    parser.parse(argv);

    // Получение значений из аргументов
    size_t bir, cw;
    extract_parameter(parser, arg::bytes, bir, 16);
    extract_parameter(parser, arg::width, cw,   8);

    // Установка параметров с обрезанием по диапазону
    const std::string file_path(parser[0]);
    const size_t bytes_in_row = std::clamp(bir, size_t{1}, base62_digits.size());
    const size_t column_width = std::clamp(cw,  size_t{1}, bytes_in_row);
    const bool colored_utf8_codes  = parser[arg::color];
    const bool disable_text_column = parser[arg::ascii];
    const bool view_zeros_as_grey  = parser[arg::zeros];

    // Говорит про выход за границы параметра
    info_out_of_range(bytes_in_row != bir, bytes_in_row, bir, "bytes in row"sv);
    info_out_of_range(column_width != cw,  column_width, cw,  "column width"sv);

    // Открытие файла
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << message_prefix::error <<
        "Cannot open file '"sv << file_path << "'\n"sv;
        return EXIT_FAILURE;
    }

    // Получение длины файла
    file.seekg(0, std::ios::end);
    size_t file_length = file.tellg();
    file.seekg(0);

    // Вывод данных о файле
    std::cout << file_path << ", "sv << file_length / 1024.l
    << " KiB ("sv << file_length << " byte)\n"sv;

    // Проверка на пустоту файла
    if (!file_length) {
        std::cout << message_prefix::info << "File is empty\n"sv;
        file.close();
        return EXIT_SUCCESS;
    }

    // Получение количества цифр в размере файла
    const size_t hex_digit_count =
        get_hex_digit_count(file_length) - (file_length == 16);

    // Вывод шапки сдвигов
    std::cout << std::string(hex_digit_count + 2, ' ') <<
    set_color(ansi_color::grey, generate_header(
        bytes_in_row, column_width, !disable_text_column)) << '\n';

    // Переменные для режима отображения UTF-8
    size_t bytes_left = 0;
    std::string curr_byte_color;

    // Построчный проход по файлу
    size_t curr_address = 0;
    while (file_length) {
        // Вывод адреса строки
        std::cout << set_color(ansi_color::grey,
            fixed_width_hex_number(curr_address, hex_digit_count));
        curr_address += bytes_in_row;

        // Срез из N байтов
        const size_t sample_size = std::min(bytes_in_row, file_length);
        std::string sample(sample_size, '\0');
        file.read(sample.data(), sample_size);
        file_length -= sample_size;

        // Вывод байтов из среза
        for (size_t i = 0; i < bytes_in_row; i++) {
            // Отступ между байтами
            std::cout << std::string(i % column_width == 0 ? 2 : 1, ' ');
            // Печать "пустого" байта
            if (i >= sample_size) { std::cout << "  "sv; continue; }

            unsigned char byte = sample[i];
            auto byte_str = fixed_width_hex_number(byte, 2);

            if (!colored_utf8_codes) {
                if (view_zeros_as_grey && !byte)
                    byte_str = set_color(ansi_color::grey, byte_str);
                std::cout << byte_str;
                continue;
            }

            // Если это начальный байт
            if (!bytes_left) {
                if (!(byte & 0x80)) { // Если ASCII символ
                    std::cout << set_color(ansi_color::utf8_blue, byte_str);
                } else if (0xc0 <= byte && byte <= 0xdf) { // 2 байта
                    std::cout << set_color(ansi_color::utf8_gren, byte_str);
                    curr_byte_color = ansi_color::utf8_pale_gren;
                    bytes_left = 1;
                } else if (0xe0 <= byte && byte <= 0xef) { // 3 байта
                    std::cout << set_color(ansi_color::utf8_yllw, byte_str);
                    curr_byte_color = ansi_color::utf8_pale_yllw;
                    bytes_left = 2;
                } else if (0xf0 <= byte && byte <= 0xf7) { // 4 байта
                    std::cout << set_color(ansi_color::utf8_red, byte_str);
                    curr_byte_color = ansi_color::utf8_pale_red;
                    bytes_left = 3;
                }
            } else { // Если остались байты
                std::cout << set_color(curr_byte_color, byte_str);
                --bytes_left;
            }

            // Если это последний байт среза или строки
            if (!bytes_left && i + 1 == sample_size)
                curr_byte_color.clear();
        }

        // Вывод буквенного представления байтов
        if (!disable_text_column) {
            std::cout << ' ';
            for (size_t i = 0; char byte : sample) {
                if (i++ % column_width == 0) std::cout << ' ';

                if (std::isprint(byte)) std::cout << byte;
                else if (is_any_whitespace(byte))
                    std::cout << set_color(ansi_color::cyan, whitespace_to_char(byte));
                else if (byte == '\0')
                    std::cout << set_color(ansi_color::grey, dot_char);
                else if (byte < -1) // Расширенная ASCII кроме 0xFF
                    std::cout << set_color(ansi_color::mgnt, byte);
                else std::cout << set_color(ansi_color::red, '?');
            }
        }

        std::cout << '\n';

        // Пауза после M байтов
        if (curr_address % (bytes_in_row * page_row_count) == 0) {
            std::cout << "Continue? [Y/n]: "sv;
            if (getchar() == 'n') break;
            std::cout << "\x1b[1A"sv; // Двигает курсор вверх
        }
    }

    file.close();
}