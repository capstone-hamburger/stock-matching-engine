#include "matching_engine/models.hpp"

namespace matching_engine {

std::string_view toString(OrderType value) noexcept
{
    switch (value) {
    case OrderType::Buy: return "BUY";
    case OrderType::Sell: return "SELL";
    }
    return "UNKNOWN";
}

std::string_view toString(OrderStatus value) noexcept
{
    switch (value) {
    case OrderStatus::Open: return "OPEN";
    case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
    case OrderStatus::Filled: return "FILLED";
    case OrderStatus::Canceled: return "CANCELED";
    case OrderStatus::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

std::string_view toString(SubmissionRejectionReason value) noexcept
{
    switch (value) {
    case SubmissionRejectionReason::None: return "NONE";
    case SubmissionRejectionReason::InvalidOrderId: return "INVALID_ORDER_ID";
    case SubmissionRejectionReason::InvalidUserId: return "INVALID_USER_ID";
    case SubmissionRejectionReason::InvalidOrderType: return "INVALID_ORDER_TYPE";
    case SubmissionRejectionReason::InvalidPrice: return "INVALID_PRICE";
    case SubmissionRejectionReason::InvalidQuantity: return "INVALID_QUANTITY";
    case SubmissionRejectionReason::DuplicateOrderId: return "DUPLICATE_ORDER_ID";
    case SubmissionRejectionReason::EngineCapacityExceeded: return "ENGINE_CAPACITY_EXCEEDED";
    }
    return "UNKNOWN";
}

std::string_view toString(CancellationFailureReason value) noexcept
{
    switch (value) {
    case CancellationFailureReason::None: return "NONE";
    case CancellationFailureReason::InvalidOrderId: return "INVALID_ORDER_ID";
    case CancellationFailureReason::InvalidUserId: return "INVALID_USER_ID";
    case CancellationFailureReason::OrderNotFound: return "ORDER_NOT_FOUND";
    case CancellationFailureReason::NotOwner: return "NOT_OWNER";
    case CancellationFailureReason::AlreadyFilled: return "ALREADY_FILLED";
    case CancellationFailureReason::AlreadyCanceled: return "ALREADY_CANCELED";
    }
    return "UNKNOWN";
}

} // namespace matching_engine
