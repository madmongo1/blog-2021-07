#ifndef WEBSERVER_ANY_WEBSOCKET_HPP
#define WEBSERVER_ANY_WEBSOCKET_HPP

#include "asio.hpp"
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/variant2/variant.hpp>

using tcp_transport = asio::ip::tcp::socket;
using tls_transport = asio::ssl::stream<tcp_transport>;

using tcp_websock = boost::beast::websocket::stream<tcp_transport>;
using tls_websock = boost::beast::websocket::stream<tls_transport>;

struct any_websocket
{
    using var_type = boost::variant2::variant<
        tcp_websock,
        tls_websock
    >;

    any_websocket(tcp_transport&& t, boost::beast::flat_buffer& rxbuf);
    any_websocket(tls_transport&& t, boost::beast::flat_buffer& rxbuf);

    void kill();

private:

    var_type ws_;
    boost::beast::flat_buffer& rxbuf_;
};

#endif
