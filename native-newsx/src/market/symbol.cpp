#include "symbol.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace newsx::market {

char Symbol::tora_exchange() const {
    if (exchange == "SH") return '1';
    if (exchange == "SZ") return '2';
    if (exchange == "BJ") return '4';
    return '\0';
}

std::optional<Symbol> parse_symbol(std::string_view value) {
    std::string text(value);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::toupper(c); });
    static const std::regex prefix(R"(^(SH|SZ|BJ)([0-9]{6})$)");
    static const std::regex suffix(R"(^([0-9]{6})\.(SH|SZ|BJ)$)");
    static const std::regex digits(R"(^[0-9]{6}$)");
    std::smatch match;
    if (std::regex_match(text, match, prefix)) return Symbol{match[1], match[2]};
    if (std::regex_match(text, match, suffix)) return Symbol{match[2], match[1]};
    if (!std::regex_match(text, digits)) return std::nullopt;
    if (text[0] == '6') return Symbol{"SH", text};
    if (text[0] == '0' || text[0] == '3') return Symbol{"SZ", text};
    if (text[0] == '4' || text[0] == '8' || text[0] == '9') return Symbol{"BJ", text};
    return std::nullopt;
}

bool valid_fronts(std::string_view value) {
    if (value.empty()) return false;
    static const std::regex front(R"(^tcp://[A-Za-z0-9_.-]+:([0-9]{1,5})$)");
    std::string text(value);
    std::size_t begin = 0;
    do {
        const auto end = text.find(',', begin);
        std::string part = text.substr(begin, end == std::string::npos ? end : end - begin);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
        std::smatch match;
        if (!std::regex_match(part, match, front)) return false;
        const auto port = std::stoi(match[1]);
        if (port < 1 || port > 65535) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return true;
}

} // namespace newsx::market
