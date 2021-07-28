#ifndef WEBSERVER_ANY_WEBSOCKET_HPP
#define WEBSERVER_ANY_WEBSOCKET_HPP

#include "asio.hpp"
#include "beast.hpp"

#include <boost/variant2/variant.hpp>
#include <string>
#include <queue>
#include <deque>
#include <span>

using tcp_transport = asio::ip::tcp::socket;
using tls_transport = asio::ssl::stream<tcp_transport>;

using tcp_websock = beast::websocket::stream<tcp_transport>;
using tls_websock = beast::websocket::stream<tls_transport>;

struct condvar
{
    using timer_type = asio::steady_timer;
    using time_point = timer_type::time_point;

    condvar(asio::any_io_executor exec)
    : timer_(std::move(exec))
    {
        timer_.expires_at(time_point::max());
    }

    void
    notify_all()
    {
        timer_.cancel();
    }

    void
    notify_one()
    {
        timer_.cancel_one();
    }

    asio::awaitable<void>
    wait()
    {
        auto [ec] = co_await timer_.async_wait(asioex::as_tuple(asio::use_awaitable));
        if (ec && ec != asio::error::operation_aborted)
            throw system_error(ec);
        co_return;
    }

    asio::steady_timer timer_;
};

struct frame
{
    frame(beast::flat_buffer const& buf, bool binary)
    : buffer_(&buf)
    , binary_(binary)
    {

    }

    std::string_view as_string() const
    {
        auto d = buffer_->data();
        return { reinterpret_cast<const char*>(d.data()), d.size() };
    }

    std::span<const char> 
    as_span() const
    {
        auto d = buffer_->data();
        return { reinterpret_cast<const char*>(d.data()), d.size() };
    }

    bool 
    is_binary() const { return binary_; }
    
    bool 
    is_text() const { return !binary_; }

private:
    beast::flat_buffer const* buffer_;
    bool binary_;
};

enum class frame_type : std::uint8_t
{
    text = 0,
    binary = 1
};

struct any_websocket
{
    using request_type = beast::http::request<beast::http::string_body>;

    any_websocket(tcp_transport&& t, beast::flat_buffer&& rxbuf);
    any_websocket(tls_transport&& t, beast::flat_buffer&& rxbuf);

    tcp_transport const& 
    socket() const;

    asio::awaitable<void>
    accept(request_type& request);

    /// Coroutine to write in order.
    /// Each subsequent invocation of this coroutine maintains order
    /// @param s is a reference to the text frame to write
    asio::awaitable<void>
    write(std::string s, frame_type type = frame_type::text);

    asio::awaitable< frame > 
    read();

    /// Initiate a close on the websocket. May be invoked while a read is in progress.
    /// @pre must not be invoked while there is an outstanding close in progress
    asio::awaitable<void>
    close(beast::websocket::close_reason reason = beast::websocket::close_code::normal);

    /// Wait for all extra coroutines on this object to end
    /// @pre read must have returned with an error
    asio::awaitable<void> 
    join();

    /// Return the websocket's executor
    asio::any_io_executor
    get_executor();

private:
    using var_type = boost::variant2::variant<
        tcp_websock,
        tls_websock
    >;

    var_type ws_;
    boost::beast::flat_buffer rxbuf_;

    condvar write_condition_;
    condvar join_condition_;
    std::queue<std::string, std::deque<std::string>> txqueue_;
    std::size_t outstanding_writes_ = 0;
    std::size_t last_read_size_ = 0;
    bool closing_ = false;
};


/// coroutine to perform write of a string to a shared websocket.
/// @param impl is a shared_ptr to the websocket
/// @param s is the string to write
/// @param type is the type of frame to write, defaults to text
/// @return awaitable void
/// @throw May throw if the write fails for any reason. This is an expected return path.
/// @note This function is provided because websockets have multiple outstanding operations against them
/// at any one time and the implementation must survive until the last remaining operation has completed.
///
asio::awaitable<void>
write(std::shared_ptr<any_websocket> impl, std::string s, frame_type type = frame_type::text);

/// Add a string to the write queue of an any_websocket and return immediately.
/// Delivery of the write is not assured.
/// The write will either be initiated immediately (if there is no other write in progress)
/// or will await its turn in the write queue.
/// @param pws is a shared_ptr to an any_websocket. The shared_ptr is necessary in case the write
/// is deferred.
/// @param s is a string containing the bytes to write
/// @param type is the type of frame to send. Defaults to text
///
void 
queue_write(std::shared_ptr<any_websocket> pws, std::string s, frame_type type = frame_type::text);

#endif
