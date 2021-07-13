#include "detail/program_stop_state.hpp"

struct program_stop_source;

struct program_stop_sink
{
    program_stop_sink(program_stop_source const& source);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler>
    auto operator()(CompletionHandler&& token) 
    -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler, void(error_code))
    {
        return state_->async_wait(std::forward<CompletionHandler>(token));
    }

    int 
    retcode() const;

    std::string const& 
    message() const;

private:
    std::shared_ptr<detail::program_stop_state> state_;
};