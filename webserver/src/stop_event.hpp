#ifndef STOP_EVENT_HPP
#define STOP_EVENT_HPP

#include "asio.hpp"

struct stop_listener;

struct stop_event
{
    explicit stop_event(asio::any_io_executor exec);

    void
    trigger();

    template < typename CompletionToken >
    auto
    operator()(CompletionToken &&token);

    bool
    triggered() const;

  private:
    friend stop_listener;
    asio::steady_timer timer_;
};

template < typename CompletionToken >
auto
stop_event::operator()(CompletionToken &&token)
{
    return timer_.async_wait(asioex::deferred([](auto) { return asioex::deferred.values(); }))(
        std::forward< CompletionToken >(token));
}

#endif
