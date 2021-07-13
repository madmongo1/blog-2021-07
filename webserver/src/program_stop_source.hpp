#ifndef PROGRAM_STOP_SOURCE
#define PROGRAM_STOP_SOURCE

#include "detail/program_stop_state.hpp"
#include <memory>

struct program_stop_sink;

struct program_stop_source
{
    program_stop_source(asio::any_io_executor exec);

    void 
    signal(int code, std::string_view message);

    void 
    signal(std::exception const& e);

private:

    friend program_stop_sink;

    std::shared_ptr<detail::program_stop_state> state_;
};

#endif
