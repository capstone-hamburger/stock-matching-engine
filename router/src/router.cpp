#include "matching_engine/router.hpp"

#include "matching_engine/engine.hpp"
#include "order_protocol.hpp"

#include <utility>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace matching_engine {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using Tcp = net::ip::tcp;
using ErrorCode = boost::system::error_code;

// All methods and socket handlers run on the router's single I/O thread.
class Session final : public std::enable_shared_from_this<Session> {
public:
    using Dispatch = std::function<void(protocol::IncomingRequest, std::weak_ptr<Session>)>;
    using OnClose = std::function<void(const std::shared_ptr<Session>&)>;

    Session(Tcp::socket socket, std::size_t maxBytes, Dispatch dispatch, OnClose onClose)
        : ws_(std::move(socket)), dispatch_(std::move(dispatch)), onClose_(std::move(onClose))
    {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.read_message_max(maxBytes);
        ws_.text(true);
    }

    void start()
    {
        ws_.async_accept([self = shared_from_this()](ErrorCode ec) {
            if (ec) {
                self->stop();
            } else {
                self->read();
            }
        });
    }

    void stop()
    {
        if (closed_) {
            return;
        }
        closed_ = true;
        ErrorCode ignored;
        beast::get_lowest_layer(ws_).socket().shutdown(Tcp::socket::shutdown_both, ignored);
        beast::get_lowest_layer(ws_).socket().close(ignored);
        onClose_(shared_from_this());
    }

    void reply(std::string response)
    {
        if (closed_) {
            return;
        }
        output_ = std::move(response);
        ws_.async_write(net::buffer(output_),
            [self = shared_from_this()](ErrorCode ec, std::size_t) {
                if (ec) {
                    self->stop();
                } else {
                    self->output_.clear();
                    self->read();
                }
            });
    }

private:
    void read()
    {
        if (closed_) {
            return;
        }
        ws_.async_read(input_, [self = shared_from_this()](ErrorCode ec, std::size_t) {
            if (ec || self->closed_) {
                self->stop();
                return;
            }
            const bool isText = self->ws_.got_text();
            auto payload = beast::buffers_to_string(self->input_.data());
            self->input_.consume(self->input_.size());
            if (!isText) {
                self->reply(protocol::error("TEXT_REQUIRED", "Send a JSON text WebSocket message"));
                return;
            }
            try {
                auto request = protocol::parseRequest(payload);
                self->dispatch_(std::move(request), self);
            } catch (const protocol::PacketError& error) {
                self->reply(protocol::error(error.code, error.what()));
            }
        });
    }

    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer input_;
    std::string output_;
    Dispatch dispatch_;
    OnClose onClose_;
    bool closed_{false};
};

} // namespace

class Router::Impl final {
public:
    explicit Impl(RouterConfig config)
        : config_(std::move(config)), engine_(config_.stockId)
    {
        if (config_.maxMessageBytes == 0 || config_.maxConnections == 0) {
            throw std::invalid_argument{"Router limits must be positive"};
        }
    }

    ~Impl() { stop(); }

    void start()
    {
        if (used_) {
            throw std::logic_error{"Router::start may only be called once"};
        }
        used_ = true;
        try {
            const Tcp::endpoint endpoint{net::ip::make_address(config_.address), config_.port};
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(Tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen(net::socket_base::max_listen_connections);
            port_ = acceptor_.local_endpoint().port();
            accept();
            engineThread_ = std::thread([this] { engineContext_.run(); });
            ioThread_ = std::thread([this] { ioContext_.run(); });
            running_ = true;
        } catch (...) {
            ErrorCode ignored;
            acceptor_.close(ignored);
            engineWork_.reset();
            if (engineThread_.joinable()) {
                engineThread_.join();
            }
            ioContext_.stop();
            throw;
        }
    }

    void stop()
    {
        if (!running_) {
            return;
        }
        // This barrier is queued on the I/O context: no new engine jobs after it.
        net::post(ioContext_, [this] {
            stopping_ = true;
            ErrorCode ignored;
            acceptor_.close(ignored);
            acceptRetry_.cancel(ignored);
            while (!sessions_.empty()) {
                // stop() erases from sessions_, so keep a local owning reference.
                const auto session = *sessions_.begin();
                session->stop();
            }
            engineWork_.reset();
        });
        // run() finishes after all previously queued engine requests finish.
        engineThread_.join();
        ioWork_.reset();
        ioThread_.join();
        running_ = false;
    }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

private:
    void accept()
    {
        acceptor_.async_accept([this](ErrorCode ec, Tcp::socket socket) {
            if (stopping_) {
                return;
            }
            if (!ec && sessions_.size() < config_.maxConnections) {
                auto session = std::make_shared<Session>(std::move(socket), config_.maxMessageBytes,
                    [this](protocol::IncomingRequest request, std::weak_ptr<Session> client) {
                        dispatch(std::move(request), std::move(client));
                    },
                    [this](const std::shared_ptr<Session>& client) { sessions_.erase(client); });
                sessions_.insert(session);
                session->start();
            }
            // Excess connections are closed when the unclaimed socket is destroyed.
            // Delay persistent accept errors (e.g. fd exhaustion) to avoid a hot loop.
            if (ec) {
                acceptRetry_.expires_after(std::chrono::milliseconds{100});
                acceptRetry_.async_wait([this](ErrorCode timerError) {
                    if (!timerError && !stopping_) {
                        accept();
                    }
                });
            } else {
                accept();
            }
        });
    }

    void dispatch(protocol::IncomingRequest request, std::weak_ptr<Session> client)
    {
        const auto stockId = std::visit([](const auto& value) { return value.stockId; }, request);
        if (stockId != config_.stockId) {
            if (const auto session = client.lock()) {
                session->reply(protocol::error("UNKNOWN_STOCK", "No engine registered for stockId"));
            }
            return;
        }
        // Asio's task queue is the thread boundary. Only this worker touches Engine.
        net::post(engineContext_, [this, request = std::move(request), client = std::move(client)] {
            std::string response;
            try {
                if (const auto* order = std::get_if<protocol::IncomingOrder>(&request)) {
                    response = protocol::orderResult(*order, engine_.submit(order->request));
                } else if (const auto* cancel = std::get_if<protocol::CancelOrderRequest>(&request)) {
                    response = protocol::cancellationResult(*cancel,
                        engine_.cancel(cancel->orderId, cancel->userId));
                } else {
                    response = protocol::currentPriceResult(engine_.stockId(), engine_.lastTradePrice());
                }
            } catch (const std::exception& error) {
                std::cerr << "Request processing failed: " << error.what() << '\n';
                const bool isQuery = std::holds_alternative<protocol::CurrentPriceRequest>(request);
                response = protocol::error("INTERNAL_ERROR", isQuery
                    ? "Current price query failed"
                    : "Order or cancellation outcome is unknown; do not retry blindly");
            }
            net::post(ioContext_, [client, response = std::move(response)]() mutable {
                if (const auto session = client.lock()) {
                    session->reply(std::move(response));
                }
            });
        });
    }

    RouterConfig config_;
    // Contexts outlive socket handlers and queued completions.
    net::io_context ioContext_{1};
    net::io_context engineContext_{1};
    net::executor_work_guard<net::io_context::executor_type> ioWork_{ioContext_.get_executor()};
    net::executor_work_guard<net::io_context::executor_type> engineWork_{engineContext_.get_executor()};
    Tcp::acceptor acceptor_{ioContext_};
    net::steady_timer acceptRetry_{ioContext_};
    Engine engine_;
    std::unordered_set<std::shared_ptr<Session>> sessions_;
    std::thread engineThread_;
    std::thread ioThread_;
    std::uint16_t port_{0};
    bool used_{false};
    bool running_{false}; // Accessed only by the controlling thread.
    bool stopping_{false}; // Accessed only by the I/O thread.
};

Router::Router(RouterConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Router::~Router() = default;
void Router::start() { impl_->start(); }
void Router::stop() { impl_->stop(); }
std::uint16_t Router::port() const noexcept { return impl_->port(); }

} // namespace matching_engine
