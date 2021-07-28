#include "asio.hpp"
#include "beast.hpp"
#include "program_stop_source.hpp"
#include "program_stop_sink.hpp"
#include "interrupt.hpp"

int 
main()
{

    auto ioc = asio::io_context();
    auto e = ioc.get_executor();

    auto stpsrc = program_stop_source(e);
    asio::co_spawn(e, 
        monitor_interrupt(stpsrc), 
        asio::detached);
    ioc.run();

    auto stpsnk = program_stop_sink(stpsrc);

    return stpsnk.retcode();
}