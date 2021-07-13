#include "asio.hpp"
#include "stop_listener.hpp"
#include "program_stop_source.hpp"
#include "program_stop_sink.hpp"

#include <iostream>

using namespace std::literals;

enum wait_disposition_t {
    success,
    cancelled,
    error
};

wait_disposition_t
disposition(error_code const& ec)
{
    if (ec) 
        if (ec == asio::error::operation_aborted)
            return cancelled;
        else
            return error;
    else
        return success;
}

auto 
monitor_interrupt(program_stop_source stopper)
-> asio::awaitable<void>
{
    auto sigs = asio::signal_set(co_await asio::this_coro::executor, SIGINT);
    auto cstate = co_await asio::this_coro::cancellation_state;

    bool done = false;
    while (!cstate.cancelled() && !done)
    {
        if (auto cslot = cstate.slot() ; cslot.is_connected())
            cslot.assign([&](asio::cancellation_type) {
                sigs.cancel();
            });

        auto [ec, sig] = co_await sigs.async_wait(asioex::as_tuple(asio::use_awaitable));

        switch(disposition(ec))
        {
            case error:
                std::cerr << "monitor_interrupt: error - " << ec << "\n";
                stopper.signal(system_error(ec));
                done = true;
                break;

            case success:
                if (sig == SIGINT)
                    stopper.signal(4, "interrupted"), done = true;
                else
                    std::clog << "warning: unexepected signal: " << sig << "\n";
                break;

            case cancelled:
                done = true;
        }
    }
}

auto
timeout(std::chrono::milliseconds d)
-> asio::awaitable<void>
{
    auto timer = asio::steady_timer(co_await asio::this_coro::executor);
    timer.expires_after(d);
    auto [ec] = co_await timer.async_wait(asioex::as_tuple(asio::use_awaitable));
}

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

