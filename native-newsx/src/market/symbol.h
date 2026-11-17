#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace newsx::market {

struct Symbol {
    std::string exchange;
    std::string code;

    [[nodiscard]] std::string full() const { return exchange + code; }
    [[nodiscard]] char tora_exchange() const;
    bool operator==(const Symbol&) const = default;
};

[[nodiscard]] std::optional<Symbol> parse_symbol(std::string_view value);
[[nodiscard]] bool valid_fronts(std::string_view value);

} // namespace newsx::market
