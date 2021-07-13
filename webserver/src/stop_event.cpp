#include "stop_event.hpp"

stop_event::stop_event(asio::any_io_executor exec)
: timer_(exec)
{
    timer_.expires_at(asio::steady_timer::time_point::max());
}

void
stop_event::trigger()
{
    timer_.expires_at(asio::steady_timer::time_point::min());
}

bool
stop_event::triggered() const
{
    return timer_.expiry() == asio::steady_timer::time_point::min();
}
