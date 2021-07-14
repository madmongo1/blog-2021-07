#ifndef WEBSERVER_ASIO__HPP
#define WEBSERVER_ASIO__HPP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/deferred.hpp>

namespace asio   = boost::asio;
namespace asioex = asio::experimental;

using error_code = boost::system::error_code;
using system_error = boost::system::system_error;

#endif
