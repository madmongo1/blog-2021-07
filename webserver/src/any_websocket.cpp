#include "any_websocket.hpp"

any_websocket::any_websocket(tcp_transport&& t, boost::beast::flat_buffer& rxbuf)
: ws_(tcp_websock(std::move(t)))
, rxbuf_(rxbuf)
{
    
}

any_websocket::any_websocket(tls_transport&& t, boost::beast::flat_buffer& rxbuf)
: ws_(tls_websock(std::move(t)))
, rxbuf_(rxbuf)
{

}

void any_websocket::kill()
{
    visit([](auto& ws)
    {
        boost::beast::get_lowest_layer(ws).close();
    }, ws_);
}
