#ifndef DETAIL__PROGRAM_STOP_STATE_HPP
#define DETAIL__PROGRAM_STOP_STATE_HPP

#include "stop_event.hpp"
#include <string_view>
#include <string>

namespace detail
{

struct program_stop_state
{
    program_stop_state(asio::any_io_executor exec);

    void
    signal(int code, std::string_view message);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler>
    auto async_wait(CompletionHandler&& token)
    -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler, void(error_code));

    int 
    retcode() const;

    std::string const& 
    message() const;

private:

    stop_event event_;
    std::string message_;
    int retcode_;
};

template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler>
auto program_stop_state::async_wait(CompletionHandler&& token)
-> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler, void(error_code))
{
    return event_(std::forward<CompletionHandler>(token));
}

}

#endif
