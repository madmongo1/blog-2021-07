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
#include <regex>
#include <functional>

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

asio::awaitable<void>
delay(std::chrono::milliseconds dur)
{
    auto t = asio::steady_timer(co_await asio::this_coro::executor, dur);
    co_await t.async_wait(asio::use_awaitable);
}

asio::awaitable<void>
write_and_close(std::shared_ptr<any_websocket> ws, std::string s)
{
    co_await ws->write(s);
    co_await delay(5s);
    co_await ws->close();
}


asio::awaitable<void>
default_websock_app(std::shared_ptr<any_websocket> ws, beast::http::request<beast::http::string_body>& request)
try
{
    std::ostringstream resp;
    resp << "Thank you for request, but target " << request.target() << " leads nowhere\n";

    asio::co_spawn(co_await asio::this_coro::executor, 
        write_and_close(ws, resp.str()), 
        asio::detached);

    for(;;)
    {
        auto frame = co_await ws->read();

        // handle read payload here
        // remember that frame is a reference
    }
}
catch(std::exception& e)
{
    auto& s = ws->socket();
    auto ec = error_code();
    auto ep = s.remote_endpoint(ec);
    if (ec)
        std::cout << object_id(__func__, ec) << "read error: " << e.what() << '\n';
    else
        std::cout << object_id(__func__, ep) << "read error: " << e.what() << '\n';
throw;    
}


using var_stream_ptr = 
    boost::variant2::variant <
        asio::ip::tcp::socket*, 
        asio::ssl::stream<asio::ip::tcp::socket>*
    >;

using http_handler_sig = 
    asio::awaitable<void>
        (beast::http::request_parser<beast::http::string_body>& parser, 
         var_stream_ptr stream, 
         beast::flat_buffer& rxbuffer);

using http_handler_element = 
    std::tuple < 
        std::regex, 
        std::function<http_handler_sig> 
    >;

asio::awaitable<void>
send_file_error(var_stream_ptr stream, 
    beast::flat_buffer& rxbuffer,
    std::string message)
{
    auto resp = beast::http::response<beast::http::string_body>();
    resp.
}

asio::awaitable<void>
handle_http_file(
    beast::http::request_parser<beast::http::string_body>& parser, 
    var_stream_ptr stream, 
    beast::flat_buffer& rxbuffer)
{
    auto& request = parser.get();
    try
    {
        auto target = request.target();
        std::smatch match;
        static const auto re = std::regex("/*file(/[^?]*)(.*)");
        auto matches = std::regex_match(match, std::begin(target), std::end(target), re);
        if (!matches)
            throw std::invalid_argument("invalid file format");

        auto& path = match[1];

        static const std::string_view illegal_sequences[] = {
            "..",
            "//"
        };
        for(auto illegal_sequence : illegal_sequences)
        {
            if(std::search(std::begin(path), std::end(path), std::begin(illegal_sequence), std::end(illegal_sequence)))
            {
                std::ostringstream ss;
                ss << "Illegal use of " << illegal_sequence << " in path name";
                throw std::invalid_argument(ss.str());
            }
        }

        if (request.method() == http::verb::get)
        {

        } 
        else
        {
            throw std::invalid_argument("Invalid method");
        }
    }
    catch(std::exception& e)
    {
        // here we should execute the error response coroutine
        // but we need special handling because you can't call
        // a coroutine in an exception handler
        std::cerr << e.what() << '\n';
    }

    
}

http_handler_element http_endpoints[] = {
    std::make_tuple(std::regex("/file/.*"), handle_http_file),
}

asio::awaitable<void>
handle_default_request(
    beast::http::request_parser<beast::http::string_body>& parser, 
    var_stream_ptr stream, 
    beast::flat_buffer& rxbuffer)
{
    bool error = false;
    auto& req = parser.get();
    auto olen = req.payload_size();
    if (!olen || *olen > 1'000'000)
        error = true;

    if (!error && !parser.is_done())
    {
        // complete reading the rest of the message
        co_await visit([&rxbuffer, &parser](auto* pstream) 
        {
            return beast::http::async_read_some(
                *pstream, 
                rxbuffer, 
                parser, 
                asio::use_awaitable);
        }, stream);
    }

    auto narrate = [](boost::optional<std::size_t> const& o) -> std::string
    {
        if (o)
            return std::to_string(*o);
        else
            return "unspecified number of";
    };

    beast::http::response<beast::http::string_body> resp;
    resp.result(beast::http::status::not_found);
    resp.set("Content-Type", "text/plain");
    std::ostringstream ss;
    ss << "Thank you for your " << req.method_string() << " request containing " << narrate(olen) << " bytes\n"
    << req.target() << " was not found on this server. Please try again.\n";
    resp.body() = ss.str();
    resp.prepare_payload();

    co_await visit([&resp](auto* pstream) {
        return 
            beast::http::async_write(
                *pstream, 
                resp, 
                asio::use_awaitable);
    }, stream);


    if(error)
        throw std::invalid_argument("request too big");
}



template<class Stream>
asio::awaitable<void>
chat_http(Stream& stream, beast::flat_buffer& rx_buffer)
{
    using namespace asioex::awaitable_operators;

    auto ident = beast::get_lowest_layer(stream).remote_endpoint();
    auto me = object_id(__func__, ident);


    auto timer = asio::steady_timer(co_await asio::this_coro::executor);

    auto again = true;
    while(again)
    {
        auto parser = beast::http::request_parser<beast::http::string_body>();

        auto which = co_await (
            read_header_only(stream, rx_buffer, parser) ||
            timeout(timer, 30s)
        );

        // break on timeout
        if(which.index() == 1)
            break;

        auto& request = parser.get();
        std::cout << me << "header received:\n" << request;

        again = !request.need_eof();
        auto const target = request.target();

        if (beast::websocket::is_upgrade(request))
        {
            // upgrade to websocket
            auto websock = std::make_shared<any_websocket>(std::move(stream), std::move(rx_buffer));
            co_await websock->accept(request);

            using generator_sig = asio::awaitable<void>(std::shared_ptr<any_websocket>, beast::http::request<beast::http::string_body>&);
            using generator_type = std::function<generator_sig>;
            using element_type = std::tuple<std::regex, generator_type>;
            static const std::vector<element_type> generators;

            for (auto&& [re, gen] : generators)
                if (std::regex_match(target.begin(), target.end(), re))
                    co_return co_await gen(websock, request);
            co_return co_await default_websock_app(websock, request);
        }
        else
        {
            auto handled = false;
            for(auto&& [handler, gen] : http_endpoints)
                if (std::regex_match(target.begin(), target.end(), re)) 
                {
                    handled = true;
                    co_return co_await handler(parser, var_stream_ptr(&stream), rx_buffer);
                    break;
                }
            // handle http request
            co_await handle_default_request(parser, var_stream_ptr(&stream), rx_buffer);
        }
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
            {
                rx_buffer.consume(std::get<0>(which));
                co_await chat_http(ssl_stream, rx_buffer);
            }
            else
            {
                std::cout << me << "handshake timeout on tls connection\n";
            }
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
