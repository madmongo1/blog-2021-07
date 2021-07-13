#include "stop_event.hpp"

struct stop_listener
{
    stop_listener(stop_event &l);

    template < typename CompletionToken >
    auto
    operator()(CompletionToken &&token);

    bool
    triggered() const;

  private:
    asio::steady_timer *timer_;
};

    template < typename CompletionToken >
    auto
    stop_listener::operator()(CompletionToken &&token)
    {
        return timer_->async_wait(asioex::deferred([](auto) { return asioex::deferred.values(); }))(
            std::forward< CompletionToken >(token));
    }
