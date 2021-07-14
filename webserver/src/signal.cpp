#include "signal.hpp"
#include "disposition.hpp"
#include <iostream>

auto wait_signal(asio::signal_set& sigs) 
-> asio::awaitable<int>
{
    if (auto cslot = (co_await asio::this_coro::cancellation_state).slot() ; cslot.is_connected())
        cslot.assign([&](asio::cancellation_type) {
            sigs.cancel();
        });
 
    co_return co_await sigs.async_wait(asio::use_awaitable);
}

auto wait_signal(int sig) 
-> asio::awaitable<int>
{
    auto sigs = asio::signal_set(co_await asio::this_coro::executor, sig);
    co_return co_await wait_signal(sigs);
}
