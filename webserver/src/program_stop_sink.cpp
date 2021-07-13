#include "program_stop_sink.hpp"
#include "program_stop_source.hpp"

program_stop_sink::program_stop_sink(program_stop_source const& source)
: state_ { source.state_ }
{

}

int 
program_stop_sink::retcode() const
{
    return state_->retcode();
}

std::string const& 
program_stop_sink::message() const
{
    return state_->message();
}
