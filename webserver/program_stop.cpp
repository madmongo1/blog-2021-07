#include "program_stop_source.hpp"
#include "program_stop_sink.hpp"
#include <iostream>

using namespace std::literals;

auto 
wait_stop(program_stop_sink& sink) 
-> asio::awaitable<void>
{
    return sink(asio::use_awaitable);
}

auto 
stop_soon(program_stop_source src)
-> asio::awaitable<void>
{
    auto timer = asio::steady_timer(co_await asio::this_coro::executor);
    timer.expires_after(1s);
    auto [ec] = co_await timer.async_wait(asioex::as_tuple(asio::use_awaitable));
    if (ec)
        std::clog << "stop_soon: " << ec << "\n";

    src.signal(4, "timed stop");
}

int main()
{
    auto ioc = asio::io_context();
    auto exec = ioc.get_executor();

    auto src = program_stop_source(exec);
    auto sink = program_stop_sink(src);

    asio::co_spawn(exec, wait_stop(sink), asio::detached);
    asio::co_spawn(exec, stop_soon(src), asio::detached);

    ioc.run();

    if (sink.retcode()) 
        std::cerr 
            << "test-program_stop: code=" << sink.retcode() 
            << " message=" << sink.message() 
            << "\n";
    return sink.retcode();
}