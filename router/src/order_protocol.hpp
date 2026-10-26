#pragma once

#include "matching_engine/models.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <variant>
#include <stdexcept>
#include <string>
#include <string_view>

namespace matching_engine::protocol {

struct IncomingOrder {
    StockId stockId;
    OrderRequest request;
};

struct CurrentPriceRequest {
    StockId stockId;
};

using IncomingRequest = std::variant<IncomingOrder, CurrentPriceRequest>;

class PacketError final : public std::runtime_error {
public:
    PacketError(std::string code, std::string message);
    std::string code;
};

[[nodiscard]] IncomingRequest parseRequest(std::string_view payload);
[[nodiscard]] std::string currentPriceResult(StockId stockId, std::optional<Price> price);
[[nodiscard]] std::string error(std::string_view code, std::string_view message);
[[nodiscard]] std::string orderResult(const IncomingOrder& order, const SubmissionResult& result);

} // namespace matching_engine::protocol
