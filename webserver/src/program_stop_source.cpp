#include "program_stop_source.hpp"

program_stop_source::program_stop_source(asio::any_io_executor exec)
: state_(std::make_shared<
    detail::program_stop_state>(
        std::move(exec)))
{

}

void 
program_stop_source::signal(int code, std::string_view message)
{
    if (state_)
    {
        state_->signal(code, message);
    }
}

void 
program_stop_source::signal(std::exception const& e)
{
    signal(127, std::string_view(e.what()));
}
