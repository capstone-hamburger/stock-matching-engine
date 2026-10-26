#include "order_protocol.hpp"

#include <array>
#include <limits>
#include <utility>

namespace matching_engine::protocol {
namespace {

using Json = nlohmann::json;

std::int64_t integer(const Json& object, const char* field, const char* errorCode = "INVALID_ORDER")
{
    const auto& value = object.at(field);
    // get<int64_t>() alone would truncate floats or wrap oversized unsigned IDs.
    if (!value.is_number_integer()
        || (value.is_number_unsigned()
            && value.get<std::uint64_t>()
                > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
        throw PacketError{errorCode, std::string{field} + " must be an int64 JSON integer"};
    }
    return value.get<std::int64_t>();
}

} // namespace

PacketError::PacketError(std::string errorCode, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(errorCode))
{
}

IncomingRequest parseRequest(std::string_view payload)
{
    Json object;
    try {
        object = Json::parse(payload, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 8) {
                throw PacketError{"INVALID_JSON", "JSON nesting is too deep"};
            }
            return true;
        });
    } catch (const Json::exception&) {
        throw PacketError{"INVALID_JSON", "Expected one valid JSON object"};
    }

    if (object.is_object() && object.contains("type")
        && object.at("type") == "GET_CURRENT_PRICE") {
        if (object.size() != 2 || !object.contains("stockId")) {
            throw PacketError{"INVALID_REQUEST",
                "GET_CURRENT_PRICE requires exactly type and stockId"};
        }
        const auto stockId = integer(object, "stockId", "INVALID_REQUEST");
        if (stockId <= 0) {
            throw PacketError{"INVALID_REQUEST", "stockId must be positive"};
        }
        return CurrentPriceRequest{stockId};
    }

    constexpr std::array fields{"orderId", "userId", "stockId", "status", "quantity", "price", "type"};
    if (!object.is_object() || object.size() != fields.size()) {
        throw PacketError{"INVALID_ORDER", "Expected exactly the seven ERD Order fields"};
    }
    for (const auto* field : fields) {
        if (!object.contains(field)) {
            throw PacketError{"INVALID_ORDER", std::string{"Missing field: "} + field};
        }
    }
    if (object.at("status") != "PENDING") {
        throw PacketError{"INVALID_ORDER", "New orders must have status PENDING"};
    }
    const auto& type = object.at("type");
    if (type != "BUY" && type != "SELL") {
        throw PacketError{"INVALID_ORDER", "type must be BUY or SELL"};
    }

    IncomingOrder order{
        .stockId = integer(object, "stockId"),
        .request = {
            .orderId = integer(object, "orderId"),
            .userId = integer(object, "userId"),
            .type = type == "BUY" ? OrderType::Buy : OrderType::Sell,
            .price = integer(object, "price"),
            .quantity = integer(object, "quantity"),
        },
    };
    if (order.stockId <= 0) {
        throw PacketError{"INVALID_ORDER", "stockId must be positive"};
    }
    return order;
}

std::string currentPriceResult(StockId stockId, std::optional<Price> price)
{
    return Json{
        {"event", "CURRENT_PRICE"},
        {"stockId", stockId},
        {"price", price ? Json(*price) : Json(nullptr)},
    }.dump();
}

std::string error(std::string_view code, std::string_view message)
{
    return Json{{"event", "ERROR"}, {"code", code}, {"message", message}}.dump();
}

std::string orderResult(const IncomingOrder& order, const SubmissionResult& result)
{
    auto trades = Json::array();
    for (const auto& trade : result.trades) {
        trades.push_back({
            {"tradeId", trade.tradeId},
            {"buyOrderId", trade.buyOrderId},
            {"sellOrderId", trade.sellOrderId},
            {"price", trade.price},
            {"quantity", trade.quantity},
        });
    }
    // The ERD has no rejected/partially-filled status. Preserve detail separately.
    Json status = nullptr;
    if (result.accepted) {
        status = result.status == OrderStatus::Filled ? "COMPLETE" : "PENDING";
    }
    return Json{
        {"event", "ORDER_RESULT"},
        {"orderId", order.request.orderId},
        {"stockId", order.stockId},
        {"accepted", result.accepted},
        {"status", status},
        {"engineStatus", toString(result.status)},
        {"rejectionReason", toString(result.rejectionReason)},
        {"filledQuantity", result.filledQuantity},
        {"remainingQuantity", result.remainingQuantity},
        {"trades", std::move(trades)},
    }.dump();
}

} // namespace matching_engine::protocol
