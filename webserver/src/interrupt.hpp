#ifndef WEBSERVER_INTERRUPT_HPP
#define WEBSERVER_INTERRUPT_HPP

#include "asio.hpp"
#include "program_stop_source.hpp"

asio::awaitable<void>
monitor_interrupt(program_stop_source stopper);

#endif
