#pragma once

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace matching_engine {

using StockId = std::int64_t;
using OrderId = std::int64_t;
using UserId = std::int64_t;
using TradeId = std::int64_t;
using Price = std::int64_t;
using Quantity = std::int64_t;

inline constexpr Price maxPrice = std::numeric_limits<std::int32_t>::max();
inline constexpr Quantity maxOrderQuantity = std::numeric_limits<std::int32_t>::max();

enum class OrderType { Buy, Sell };

enum class OrderStatus {
    Open,
    PartiallyFilled,
    Filled,
    Canceled,
    Rejected,
};

enum class SubmissionRejectionReason {
    None,
    InvalidOrderId,
    InvalidUserId,
    InvalidOrderType,
    InvalidPrice,
    InvalidQuantity,
    DuplicateOrderId,
    EngineCapacityExceeded,
};

enum class CancellationFailureReason {
    None,
    InvalidOrderId,
    InvalidUserId,
    OrderNotFound,
    NotOwner,
    AlreadyFilled,
    AlreadyCanceled,
};

struct OrderRequest {
    OrderId orderId;
    UserId userId;
    OrderType type;
    Price price;
    Quantity quantity;
};

struct Trade {
    TradeId tradeId;
    OrderId buyOrderId;
    OrderId sellOrderId;
    Price price;
    Quantity quantity;
};

struct SubmissionResult {
    bool accepted{false};
    SubmissionRejectionReason rejectionReason{SubmissionRejectionReason::None};
    OrderStatus status{OrderStatus::Rejected};
    Quantity filledQuantity{0};
    Quantity remainingQuantity{0};
    std::vector<Trade> trades;
};

struct CancellationResult {
    bool success{false};
    Quantity canceledQuantity{0};
    CancellationFailureReason failureReason{CancellationFailureReason::None};
};

struct OrderSnapshot {
    OrderId orderId;
    UserId userId;
    OrderType type;
    Price price;
    Quantity originalQuantity;
    Quantity filledQuantity;
    Quantity canceledQuantity;
    Quantity remainingQuantity;
    OrderStatus status;
};

struct PriceLevel {
    Price price;
    Quantity quantity;
};

struct OrderBookSnapshot {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

[[nodiscard]] std::string_view toString(OrderType value) noexcept;
[[nodiscard]] std::string_view toString(OrderStatus value) noexcept;
[[nodiscard]] std::string_view toString(SubmissionRejectionReason value) noexcept;
[[nodiscard]] std::string_view toString(CancellationFailureReason value) noexcept;

} // namespace matching_engine
