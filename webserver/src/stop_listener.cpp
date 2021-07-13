#include "stop_listener.hpp"

    stop_listener::stop_listener(stop_event &l)
    : timer_(&l.timer_)
    {
    }

    bool
    stop_listener::triggered() const
    {
        return timer_->expiry() == asio::steady_timer::time_point::min();
    }
