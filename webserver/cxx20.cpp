#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>

namespace asio = boost::asio;
namespace asioex = boost::asio::experimental;
namespace beast = boost::beast;

asio::awaitable<void> 
listen()
try
{
    std::cout << "creating acceptor\n";
    auto acceptor = asio::ip::tcp::acceptor(co_await asio::this_coro::executor);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 8080));
    acceptor.listen();
    auto sock = co_await acceptor.async_accept(asio::use_awaitable);
    std::cout << "accepted: " << sock.remote_endpoint() << "\n";
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
}

int main()
{
    std::cout << "Hello, World!\n";

    asio::io_context ioc;


    asio::co_spawn(ioc.get_executor(), co_main(), asio::detached);

    auto spins = ioc.run();
    std::cout << "spins: " << spins << "\n";
}