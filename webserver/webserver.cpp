#include "program_stop_source.hpp"
#include "program_stop_sink.hpp"
#include "any_websocket.hpp"

#include "asio.hpp"
#include "signal.hpp"

#include <boost/beast.hpp>
#include <boost/mp11/tuple.hpp>

#include <iostream>
#include <iomanip>
#include <string_view>

namespace beast  = boost::beast;

using namespace std::literals;

template<asio::cancellation_type Test>
bool cancel_check(asio::cancellation_type in)
{
    return (in & Test) != asio::cancellation_type::none;
}

template<class T, class = void>
struct emitter
{
    void operator()(std::ostream& os) const
    {
        os << arg;
    }

    T& arg;
};

template<class T>
emitter(T&) -> emitter<T>;

template<class T>
std::ostream&
operator<<(std::ostream& os, emitter<T> const& e)
{
    e(os);
    return os;
}

template<class T>
struct emitter <
    T, 
    std::enable_if_t<
        std::is_same_v<
            std::decay_t<T>, 
            asio::ip::tcp::socket
        > ||
        std::is_same_v<
            std::decay_t<T>,
            asio::basic_stream_socket<asio::ip::tcp>
        >
    >
>
{
    void operator()(std::ostream& os) const
    {
        auto ec = error_code();
        auto ep = arg.remote_endpoint(ec);
        if (ec)
            os << "unconnected";
        else
            os << arg.remote_endpoint();
    }

    T& arg;
};

template<class T>
auto emit(T& x, std::ostream& os = std::cout)
{
    os << emitter<T>{ x };
}

template<class...Params>
struct object_id
{
    object_id(std::string_view name_, Params&...params_)
    : name { name_ }
    , params { params_ ... }
    {
    }

    friend 
    std::ostream&
    operator << (std::ostream& os, object_id const& oid) 
    {
        os << oid.name;
        if constexpr (sizeof...(Params) > 0)
        {
            const char* sep = "[";
            boost::mp11::tuple_for_each(oid.params, 
                [&os, &sep](auto&& x)
            {
                os << sep;
                sep = ", ";
                emit(x, os);
            });
            os << ']';
        }
        os << " : ";

        return os;
    }

    std::string_view name;
    std::tuple<Params&...> params;
};


template<class...Params>
object_id(std::string_view, Params&...) -> object_id<Params...>;


template<class...Contexts>
void
report(std::exception const& e, std::string_view location, Contexts&&...contexts)
{
    std::cerr << object_id(location, contexts...) << " : " << e.what() << '\n';
}


asio::awaitable< bool >
detect_ssl(asio::ip::tcp::socket &sock, beast::flat_buffer &buf)
try
{
    // Beast's handlers do net yet understand Asio's implicit cancellation, so we must manually wire
    // up the cancellation lambda if cancellation is enabled on the current coroutine.
    if (auto cslot = (co_await asio::this_coro::cancellation_state).slot() ; cslot.is_connected())
    {
        cslot.assign([&](asio::cancellation_type type) 
        {
            if (cancel_check<asio::cancellation_type::terminal>(type))
                sock.close(); 
        });
    }

    // note that upon resumption from a suspension point, the cancellation slot is automatically cleared
    co_return co_await 
        beast::async_detect_ssl(sock, buf, asio::use_awaitable);
    // which prevents a segfault if cancellation occurs while the coroutine is tearing down.
}
catch(std::exception& e)
{
    report(e, __func__, sock);
    throw;
}

asio::awaitable<void> 
timeout(asio::steady_timer& timer, std::chrono::milliseconds duration)
{
    timer.expires_after(duration);
    co_await timer.async_wait(asio::use_awaitable);
}

template<class Stream>
asio::awaitable<std::size_t>
read_header_only(Stream& stream, 
    beast::flat_buffer& rx_buffer, 
    beast::http::request_parser<beast::http::string_body>& parser)
{
    // Beast's handlers do net yet understand Asio's implicit cancellation, so we must manually wire
    // up the cancellation lambda if cancellation is enabled on the current coroutine.
    if (auto cslot = (co_await asio::this_coro::cancellation_state).slot() ; cslot.is_connected())
    {
        cslot.assign([&](asio::cancellation_type type) 
        {
            if (cancel_check<asio::cancellation_type::terminal>(type))
                beast::get_lowest_layer(stream).close(); 
        });
    }

    co_return co_await
        beast::http::async_read_header(stream, 
            rx_buffer, 
            parser, 
            asio::use_awaitable);
}

template<class Stream>
asio::awaitable<void>
chat_http(Stream& stream, beast::flat_buffer& rx_buffer)
{
    using namespace asioex::awaitable_operators;

    auto ident = beast::get_lowest_layer(stream).remote_endpoint();
    auto me = object_id(__func__, ident);

    auto parser = beast::http::request_parser<beast::http::string_body>();
    auto request = beast::http::request<beast::http::string_body>();

    auto timer = asio::steady_timer(co_await asio::this_coro::executor);

    auto which = co_await (
        read_header_only(stream, rx_buffer, parser) ||
        timeout(timer, 30s)
    );

    auto& header = parser.get();
    std::cout << me << "header received:\n" << header;

    if (beast::websocket::is_upgrade(request))
    {
        // upgrade to websocket
        auto websock = any_websocket(std::move(stream), rx_buffer);

    }
    else
    {
        // handle http request
    }


}

asio::awaitable< void >
chat(asio::ip::tcp::socket sock, asio::ssl::context& sslctx)
{
    using namespace asioex::awaitable_operators;

    auto ident = sock.remote_endpoint();

    try
    {
        auto const me = object_id(__func__, ident);
        std::cout << me << "accepted\n";

        auto timer     = asio::steady_timer(co_await asio::this_coro::executor);
        auto rx_buffer = beast::flat_buffer();
        auto which = co_await(
            detect_ssl(sock, rx_buffer) || 
            timeout(timer, 5s)
        );

        if (which.index() == 1)
        {
            std::cout << me << "client didn't speak\n";
            co_return;
        }

        if (auto is_ssl = std::get<0>(which) ; is_ssl)
        {
            std::cout << me << "ssl detected\n";
            auto ssl_stream = asio::ssl::stream<asio::ip::tcp::socket>(std::move(sock), sslctx);
            auto which = co_await (
                ssl_stream.async_handshake(
                    asio::ssl::stream_base::server, 
                    rx_buffer.data(),
                    asio::use_awaitable) ||
                timeout(timer, 5s)
            );

            if (which.index() == 0)
                rx_buffer.consume(std::get<0>(which));
            co_await chat_http(ssl_stream, rx_buffer);
        }
        else
        {
            std::cout << me << "tcp detected\n";
            co_await chat_http(sock, rx_buffer);
        }

        std::cout << me << "exit\n";
    }
    catch(const std::exception& e)
    {
        report(e, __func__, ident);
    }
}

void 
start_listening(asio::ip::tcp::acceptor& acceptor, asio::ip::address_v4 address, unsigned short port)
{
    using namespace asio::ip;

    acceptor.open(tcp::v4());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(tcp::endpoint(address, port));
    acceptor.listen();

}

asio::awaitable< void >
listen(program_stop_sink pstop, asio::ssl::context& sslctx)
try
{
    using namespace asioex::awaitable_operators;

    std::cout << "creating acceptor\n";
    auto acceptor = asio::ip::tcp::acceptor(co_await asio::this_coro::executor);
    start_listening(acceptor, asio::ip::address_v4::any(), 8080);

    for (;;)
    {
        std::cout << object_id(__func__) << "accepting...\n";
        auto sock = asio::ip::tcp::socket(co_await asio::this_coro::executor);
        co_await acceptor.async_accept(sock, asio::use_awaitable);
        auto ident = sock.remote_endpoint();
        std::cout << object_id(__func__) << "connection accepted from " << ident << '\n';

        auto connection_end = [ident](std::exception_ptr ep)
        {
            try {
                if (ep) 
                    std::rethrow_exception(ep);
                std::cout << object_id("connection", ident) << "ended without exception\n";
            }
            catch(std::exception& e)
            {
                std::cerr << object_id("connection", ident) << "exception : " << e.what() << '\n';
            }

        };

        // spawn a new connection, but make sure it receives a cancel signal if the program_stop_sink
        // is signalled. i.e. if the program wants to stop, the connections should shutdown gracefully
        // at their earliest convenience. 
        asio::co_spawn(
            co_await asio::this_coro::executor,
            chat(std::move(sock), sslctx) || 
            pstop(asio::use_awaitable),
            asio::detached);
    }

    std::cout << object_id(__func__) << "exit\n";
}
catch (std::exception &e)
{
    std::cerr << object_id("listen") << "exception : " << e.what() << '\n';
}

auto
monitor_sigint(program_stop_source pstop) -> asio::awaitable< void >
try
{
    auto sigs = asio::signal_set(co_await asio::this_coro::executor, SIGINT);

    static const char *msgs[] = { "First interrupt. Press ctrl-c again to stop the program.\n",
                                  "Really?\n",
                                  "You asked for it!\n" };

    for (auto &msg : msgs)
    {
        co_await wait_signal(sigs);
        std::cout << msg;
    }
    pstop.signal(4, "interrupted");
    std::cout << object_id(__func__) << "exit\n";
}
catch(std::exception& e)
{
    std::cout << object_id(__func__) << "exception : " << e.what() << "\n";
    throw;
}

asio::awaitable< void >
co_main(program_stop_source pstop, asio::ssl::context& sslctx)
{
    using namespace asioex::awaitable_operators;

    co_await(
        listen(pstop, sslctx) || 
        monitor_sigint(pstop)
    );
}

auto 
run_program()
-> program_stop_sink
{
        auto sslctx = asio::ssl::context(asio::ssl::context_base::tls_server);
        auto ioc = asio::io_context();
        auto exec = ioc.get_executor();
        auto pstop = program_stop_source(exec);
        auto stopsink = program_stop_sink(pstop);
        asio::co_spawn(ioc, 
            co_main(std::move(pstop), sslctx), 
            asio::detached);
        ioc.run();
        return stopsink;
}

int
main()
{
    const auto stopsink = run_program();

    if (stopsink.retcode())
        std::cerr << "webserver: " << stopsink.message() << '\n';
    return stopsink.retcode();
}
