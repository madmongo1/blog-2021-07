#include "detail/program_stop_state.hpp"

namespace detail
{

program_stop_state::program_stop_state(asio::any_io_executor exec)
: event_ { std::move(exec) }
, message_ { }
, retcode_ { 0 }
{
}

void
program_stop_state::signal(int code, std::string_view message)
{
    if (!retcode_)
    {
        retcode_ = code;
        message_.assign(message.begin(), message.end());
        event_.trigger();
    }
}

int 
program_stop_state::retcode() const
{
    return retcode_;
}

std::string const& 
program_stop_state::message() const
{
    return message_;
}

}
