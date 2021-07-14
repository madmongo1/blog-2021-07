#ifndef WEBSERVER_SIGNAL_HPP
#define WEBSERVER_SIGNAL_HPP

#include "asio.hpp"

auto wait_signal(asio::signal_set& sigs) 
-> asio::awaitable<int>;

auto wait_signal(int sig) 
-> asio::awaitable<int>;


#endif