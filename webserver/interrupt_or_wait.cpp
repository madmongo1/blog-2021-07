#include "asio.hpp"
#include "stop_listener.hpp"
#include "program_stop_source.hpp"
#include "program_stop_sink.hpp"
#include "signal.hpp"

#include <iostream>

using namespace std::literals;


void 
report(std::exception_ptr ep, std::variant<std::monostate, std::monostate> which)
{
    try
    {
        if(ep)
            std::rethrow_exception(ep);

        switch(which.index())
        {
            case 0:
                std::cout << "interrupted\n";
                break;

            case 1:
                std::cout << "timed out\n";
                break;
        }
    }
    catch(std::exception& e)
    {
        std::cout << "exception: " << e.what() << "\n";
    }
}

auto 
monitor_interrupt(program_stop_source stopper)
-> asio::awaitable<void>
try
{
    // wait for a signal to occur, or for the signal wait to be cancelled
    // by our implicit cancellation
    auto sig = co_await wait_signal(SIGINT);

    // if the coroutine didn't throw, then it completed having detected the sigint
    stopper.signal(sig, "interrupted");
}
catch(system_error& e)
{
    if (e.code() != asio::error::operation_aborted)
        std::cerr << __func__ << " : " << e.what() << '\n';
    throw;
}

auto
timeout(std::chrono::milliseconds d)
-> asio::awaitable<void>
{
    auto timer = asio::steady_timer(co_await asio::this_coro::executor);
    timer.expires_after(d);
    auto [ec] = co_await timer.async_wait(asioex::as_tuple(asio::use_awaitable));
}

int main()
{
    using namespace asioex::awaitable_operators;

    auto ioc = asio::io_context();
    auto exec = ioc.get_executor();

    auto src = program_stop_source(exec);
    auto sink = program_stop_sink(src);

    asio::co_spawn(
        exec, 
        monitor_interrupt(src) ||
        timeout(5s), 
        report);
    ioc.run();


    if (sink.retcode())
        std::cerr << "interrupt_or_wait: " << sink.message() << "\n";
    return sink.retcode();
}

