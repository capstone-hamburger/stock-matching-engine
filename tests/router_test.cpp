#include "matching_engine/router.hpp"

#include <utility>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using Tcp = net::ip::tcp;
using Json = nlohmann::json;
using matching_engine::Router;
using matching_engine::RouterConfig;

void expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error{message};
    }
}

Json order(std::int64_t id, std::string type = "SELL", int quantity = 2, int price = 10100)
{
    return {
        {"orderId", id}, {"userId", type == "BUY" ? 10 : 20}, {"stockId", 1001},
        {"status", "PENDING"}, {"quantity", quantity}, {"price", price}, {"type", type},
    };
}

class Client final {
public:
    explicit Client(std::uint16_t port)
    {
        ws.next_layer().connect({net::ip::make_address("127.0.0.1"), port});
        ws.handshake("127.0.0.1:" + std::to_string(port), "/");
        ws.text(true);
    }

    void send(const Json& packet) { sendText(packet.dump()); }
    void sendText(const std::string& text) { ws.write(net::buffer(text)); }

    Json receive()
    {
        beast::flat_buffer buffer;
        ws.read(buffer);
        expect(ws.got_text(), "responses must be text frames");
        return Json::parse(beast::buffers_to_string(buffer.data()));
    }

    Json request(const Json& packet)
    {
        send(packet);
        return receive();
    }

    net::io_context context;
    websocket::stream<Tcp::socket> ws{context};
};

void testMatchingAndValidation()
{
    Router router{RouterConfig{.port = 0}};
    router.start();
    expect(router.port() != 0, "port zero must resolve to a bound port");

    // An unfinished handshake must not block accept or other clients' orders.
    net::io_context rawContext;
    Tcp::socket unfinishedHandshake{rawContext};
    unfinishedHandshake.connect({net::ip::make_address("127.0.0.1"), router.port()});
    Client seller{router.port()};
    Client buyer{router.port()};

    auto result = seller.request(order(2001, "SELL", 7));
    expect(result["accepted"] == true && result["status"] == "PENDING"
        && result["engineStatus"] == "OPEN" && result["remainingQuantity"] == 7,
        "sell must rest in the engine");
    result = buyer.request(order(1001, "BUY", 3, 10200));
    expect(result["event"] == "ORDER_RESULT" && result["status"] == "COMPLETE"
        && result["engineStatus"] == "FILLED" && result["filledQuantity"] == 3,
        "buy must fill against another connection's sell");
    expect(result["trades"].size() == 1 && result["trades"][0]["price"] == 10100
        && result["trades"][0]["sellOrderId"] == 2001
        && result["trades"][0]["buyOrderId"] == 1001,
        "response must contain the real engine trade at the resting price");

    result = buyer.request(order(1002, "BUY", 6, 10200));
    expect(result["status"] == "PENDING" && result["engineStatus"] == "PARTIALLY_FILLED"
        && result["filledQuantity"] == 4 && result["remainingQuantity"] == 2,
        "ERD PENDING must retain partial fill details");
    result = buyer.request(order(1001, "BUY"));
    expect(result["accepted"] == false && result["status"].is_null()
        && result["rejectionReason"] == "DUPLICATE_ORDER_ID",
        "duplicate requests must be rejected without another execution");

    auto invalid = order(3000);
    invalid["stockId"] = 9999;
    expect(seller.request(invalid)["code"] == "UNKNOWN_STOCK", "unregistered stock must be rejected");

    for (const auto* text : {"{", "[]", "null", "{}", "true", "{} {}"}) {
        seller.sendText(text);
        expect(seller.receive()["event"] == "ERROR", "malformed or non-order JSON must be rejected");
    }
    for (const auto* field : {"orderId", "userId", "stockId", "quantity", "price", "status", "type"}) {
        invalid = order(3000);
        invalid.erase(field);
        expect(seller.request(invalid)["code"] == "INVALID_ORDER", "missing fields must be rejected");
    }
    for (const auto& value : {Json(1.5), Json("1"), Json(true), Json(nullptr)}) {
        invalid = order(3000);
        invalid["quantity"] = value;
        expect(seller.request(invalid)["code"] == "INVALID_ORDER", "non-integer quantity must be rejected");
    }
    invalid = order(3000);
    invalid["orderId"] = std::numeric_limits<std::uint64_t>::max();
    expect(seller.request(invalid)["code"] == "INVALID_ORDER", "uint64 overflow must not wrap");
    invalid["orderId"] = 1e30;
    expect(seller.request(invalid)["code"] == "INVALID_ORDER", "floating point IDs must be rejected");
    invalid = order(3000);
    invalid["status"] = "COMPLETE";
    expect(seller.request(invalid)["code"] == "INVALID_ORDER", "client cannot submit a completed order");
    invalid = order(3000);
    invalid["type"] = "sell";
    expect(seller.request(invalid)["code"] == "INVALID_ORDER", "type must match the ERD enum");
    invalid = order(3000);
    invalid["extra"] = 1;
    expect(seller.request(invalid)["code"] == "INVALID_ORDER", "unexpected fields must be rejected");
    seller.sendText(std::string(20, '[') + "0" + std::string(20, ']'));
    expect(seller.receive()["code"] == "INVALID_JSON", "excessive nesting must be rejected");

    expect(seller.request(order(3000, "SELL", 0))["rejectionReason"] == "INVALID_QUANTITY",
        "engine validation must still run");
    expect(seller.request(order(3000, "SELL", 2, 20000))["accepted"] == true,
        "a corrected order must work after previous validation failures");
    invalid = order(std::numeric_limits<std::int64_t>::max(), "SELL", 1, 20000);
    expect(seller.request(invalid)["orderId"] == std::numeric_limits<std::int64_t>::max(),
        "int64 IDs must round-trip without precision loss");

    seller.ws.binary(true);
    seller.send(order(4000));
    expect(seller.receive()["code"] == "TEXT_REQUIRED", "binary packets must be rejected");
    seller.ws.text(true);
    seller.ws.ping({});
    const auto fragmented = order(4000, "SELL", 1, 20000).dump();
    const auto split = fragmented.size() / 2;
    seller.ws.write_some(false, net::buffer(fragmented.data(), split));
    seller.ws.write_some(true, net::buffer(fragmented.data() + split, fragmented.size() - split));
    expect(seller.receive()["accepted"] == true, "fragmented messages and ping must be supported");

    // Shutdown must finish with open sockets and an unfinished HTTP upgrade.
    const auto port = router.port();
    router.stop();
    router.stop();
    Router replacement{RouterConfig{.port = port}};
    replacement.start();
    expect(Client{replacement.port()}.request(order(1))["accepted"] == true,
        "stop must release the listener so a new router can bind");
}

void testCurrentPrice()
{
    Router router{RouterConfig{.port = 0}};
    router.start();
    Client client{router.port()};
    Client peer{router.port()};
    const Json query{{"type", "GET_CURRENT_PRICE"}, {"stockId", 1001}};
    const auto expectPrice = [](const Json& result, const Json& price) {
        expect(result == Json{{"event", "CURRENT_PRICE"}, {"stockId", 1001}, {"price", price}},
            "current price response must contain the stock and last actual trade price");
    };

    expectPrice(client.request(query), nullptr);
    expect(client.request(order(1, "SELL", 2, 10100))["accepted"] == true, "sell must rest");
    expectPrice(client.request(query), nullptr);
    expect(client.request(order(2, "SELL", 3, 10200))["accepted"] == true, "second sell must rest");

    // A pipelined query must observe the entire preceding order.
    client.send(order(3, "BUY", 5, 11000));
    client.send(query);
    const auto result = client.receive();
    expect(result["trades"].size() == 2 && result["trades"][0]["price"] == 10100
        && result["trades"][1]["price"] == 10200, "buy must execute at both resting prices");
    expectPrice(client.receive(), 10200);
    expectPrice(peer.request(query), 10200);

    expect(client.request(order(4, "BUY", 1, 9000))["accepted"] == true, "unmatched buy must rest");
    expectPrice(client.request(query), 10200);
    expect(client.request(order(3, "BUY"))["accepted"] == false, "duplicate must be rejected");
    expectPrice(client.request(query), 10200);

    for (const auto& stockId : {Json(0), Json(-1), Json(1.5), Json("1001"), Json(true),
             Json(nullptr), Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto invalid = query;
        invalid["stockId"] = stockId;
        expect(client.request(invalid)["code"] == "INVALID_REQUEST", "query stockId must be positive int64");
    }
    auto invalid = query;
    invalid.erase("stockId");
    expect(client.request(invalid)["code"] == "INVALID_REQUEST", "query stockId is required");
    invalid = query;
    invalid["price"] = 1;
    expect(client.request(invalid)["code"] == "INVALID_REQUEST", "query extra fields must be rejected");
    invalid = query;
    invalid["stockId"] = 9999;
    expect(client.request(invalid)["code"] == "UNKNOWN_STOCK", "query cannot read another stock");
    invalid = query;
    invalid["type"] = "get_current_price";
    expect(client.request(invalid)["event"] == "ERROR", "request type is case sensitive");
    expectPrice(client.request(query), 10200);

    // Reads must not modify resting orders or consume trade IDs.
    const auto next = peer.request(order(5, "SELL", 1, 9000));
    expect(next["trades"].size() == 1 && next["trades"][0]["tradeId"] == 3
        && next["trades"][0]["buyOrderId"] == 4, "queries must not mutate engine state");
    expectPrice(client.request(query), 9000);

    Router other{RouterConfig{.port = 0, .stockId = 2002}};
    other.start();
    expect(Client{other.port()}.request(Json{{"type", "GET_CURRENT_PRICE"}, {"stockId", 2002}})
        == Json{{"event", "CURRENT_PRICE"}, {"stockId", 2002}, {"price", nullptr}},
        "current price must use configured stock and start without a price");
}

void testOrderingAndConcurrentConnections()
{
    Router router{RouterConfig{.port = 0}};
    router.start();
    Client client{router.port()};
    client.send(order(1));
    client.send(order(2));
    expect(client.receive()["orderId"] == 1 && client.receive()["orderId"] == 2,
        "pipelined requests and replies must preserve per-connection order");
    const auto result = client.request(order(3, "BUY", 4));
    expect(result["trades"].size() == 2 && result["trades"][0]["sellOrderId"] == 1
        && result["trades"][1]["sellOrderId"] == 2,
        "FIFO priority must survive router dispatch");

    const auto sendOrders = [&router](int firstId) {
        Client peer{router.port()};
        for (int index = 0; index < 30; ++index) {
            const auto id = firstId + index;
            const auto response = peer.request(order(id, "SELL", 1, 20000));
            expect(response["accepted"] == true && response["orderId"] == id,
                "concurrent clients must receive their own successful responses");
        }
        return peer.request(order(9999, "SELL", 1, 20000))["accepted"].get<bool>();
    };
    auto first = std::async(std::launch::async, sendOrders, 100);
    auto second = std::async(std::launch::async, sendOrders, 200);
    const bool firstAccepted = first.get();
    const bool secondAccepted = second.get();
    expect(firstAccepted != secondAccepted, "the same ID submitted by two clients must execute only once");
    client.ws.close(websocket::close_code::normal);
}

void testLifecycleAndLimits()
{
    Router neverStarted;
    neverStarted.stop();
    bool invalidConfig = false;
    try {
        Router invalid{RouterConfig{.stockId = 0}};
    } catch (const std::invalid_argument&) {
        invalidConfig = true;
    }
    expect(invalidConfig, "invalid engine config must fail before starting threads");

    Router router{RouterConfig{.port = 0, .maxMessageBytes = 1024}};
    router.start();
    bool duplicateStart = false;
    try {
        router.start();
    } catch (const std::logic_error&) {
        duplicateStart = true;
    }
    expect(duplicateStart, "a second start must fail predictably");
    bool bindFailed = false;
    try {
        Router conflict{RouterConfig{.port = router.port()}};
        conflict.start();
    } catch (const boost::system::system_error&) {
        bindFailed = true;
    }
    expect(bindFailed, "bind errors must be reported to the caller");
    Client oversized{router.port()};
    oversized.sendText(std::string(1025, 'x'));
    beast::flat_buffer buffer;
    boost::system::error_code ec;
    oversized.ws.read(buffer, ec);
    expect(ec == websocket::error::closed
        && oversized.ws.reason().code == websocket::close_code::too_big,
        "oversized messages must close with WebSocket code 1009");
    expect(Client{router.port()}.request(order(10))["accepted"] == true,
        "one failed client must not stop the server");

    Router limited{RouterConfig{.port = 0, .maxConnections = 1}};
    limited.start();
    Client active{limited.port()};
    bool limitEnforced = false;
    try {
        Client excess{limited.port()};
    } catch (const boost::system::system_error&) {
        limitEnforced = true;
    }
    expect(limitEnforced, "connection count must be bounded");
    expect(active.request(order(1))["accepted"] == true, "existing connection must remain usable");

    // Stop with a request being processed or a response waiting for its reader.
    active.send(order(2));
    limited.stop();
}

} // namespace

int main()
{
    try {
        testMatchingAndValidation();
        testCurrentPrice();
        testOrderingAndConcurrentConnections();
        testLifecycleAndLimits();
        std::cout << "WebSocket router integration tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
