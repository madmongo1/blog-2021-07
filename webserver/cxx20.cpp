#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>
#include <boost/system/system_error.hpp>
#include <string_view>

namespace asio = boost::asio;
namespace asioex = boost::asio::experimental;
namespace beast = boost::beast;
using namespace std::literals;

template<class Stream, class T>
void emit(Stream& s, T&& x)
{
    s << x;
}

template<class...Idents>
void report(std::exception const& e, std::string_view context, Idents&&...idents)
{
    std::cout << context;
    if constexpr (sizeof...(idents) != 0)
    {
        auto sep = "["sv;
        (
            (
                emit(std::cout, sep),
                emit(std::cout, idents),
                sep = ", "
            ), ...
        );
        emit(std::cout, ']');
    }
    emit(std::cout, " : ");
    emit(std::cout, e.what());
    emit(std::cout, '\n');
}

asio::awaitable<bool> 
detect_ssl(asio::ip::tcp::socket& sock, beast::flat_buffer& buf)
{
    /*
    auto cstate = (co_await asio::this_coro::cancellation_state);
    cstate.slot().assign([&](asio::cancellation_type type) {
        sock.close();
    });
    */

    std::cout << "detecting ssl\n";
    auto size = co_await sock.async_read_some(buf.prepare(1024), asio::use_awaitable);
    buf.commit(size);

    std::cout << "done read\n";
    co_return false;//co_await beast::async_detect_ssl(sock, buf, asio::use_awaitable);
}

asio::awaitable<void>
chat(asio::ip::tcp::socket sock)
{
    using namespace asioex::awaitable_operators;

    auto ident = sock.remote_endpoint();
    auto timer = asio::steady_timer(co_await asio::this_coro::executor);
    try
    {
        std::cout << "accepted: " << sock.remote_endpoint() << "\n";
        auto buf = beast::flat_buffer();
        timer.expires_after(1s);
        std::cout << "detecting about to chat\n";

        auto which = co_await (
            detect_ssl(sock, buf) || 
            timer.async_wait(asio::use_awaitable)
        );
    }
    catch(std::exception& e)
    {
        report(e, "chat"sv, ident);
    }
    std::cout << "chat over\n";
    co_return;
}

asio::awaitable<void> 
listen()
try
{
    std::cout << "creating acceptor\n";
    auto acceptor = asio::ip::tcp::acceptor(co_await asio::this_coro::executor);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 8080));
    acceptor.listen();
    for(;;)
    {
        auto [ec, sock] = co_await acceptor.async_accept(asioex::as_tuple(asio::use_awaitable));
        if (ec) {
            if (ec == asio::error::connection_aborted) continue;
            else if (ec == asio::error::operation_aborted) break;
            else throw boost::system::system_error(ec);
        }
        asio::co_spawn(co_await asio::this_coro::executor, 
            chat(std::move(sock)), 
            asio::bind_cancellation_slot(
                (co_await asio::this_coro::cancellation_state).slot(), 
                asio::detached));

    }
}
catch(std::exception& e)
{
    std::cout << "listen: exception: " << e.what() << "\n";
}

asio::awaitable<void>
monitor_sigint()
{
    auto sigs = asio::signal_set(co_await asio::this_coro::executor, SIGINT);

    static const char*msg[] = {
        "First interrupt. Press ctrl-c again to stop the program.\n",
        "Really?\n",
        "You asked for it!\n"
    };

    for (int pass = 0 ; pass < std::extent_v<decltype(msg)> ; ++pass)
    {
        co_await sigs.async_wait(asio::use_awaitable);
        std::cout << msg[pass];
    }
}

asio::awaitable<void>
co_main()
{
    using namespace asioex::awaitable_operators;

    co_await(listen() || monitor_sigint());
//    co_await(listen());
}

int main()
{
    std::cout << "Hello, World!\n";

    asio::io_context ioc;


    asio::co_spawn(ioc.get_executor(), co_main(), asio::detached);

    auto spins = ioc.run();
    std::cout << "spins: " << spins << "\n";
}