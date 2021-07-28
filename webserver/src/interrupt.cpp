#include "interrupt.hpp"
#include "signal.hpp"
#include <iostream>

asio::awaitable<void>
monitor_interrupt(program_stop_source stopper)
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
