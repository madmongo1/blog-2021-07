#ifndef WEBSERVER_PROGRAM_STOP_SINK__HPP
#define WEBSERVER_PROGRAM_STOP_SINK__HPP

#include "detail/program_stop_state.hpp"

struct program_stop_source;

struct program_stop_sink
{
    program_stop_sink(program_stop_source const& source);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionHandler, void(error_code))
    operator()(CompletionHandler&& token);

    int 
    retcode() const;

    std::string const& 
    message() const;

private:
    std::shared_ptr<detail::program_stop_state> state_;
};

template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionHandler, void(error_code))
program_stop_sink::operator()(CompletionHandler&& token) 
{
    return state_->async_wait(std::forward<CompletionHandler>(token));
}

#endif
