#include "any_websocket.hpp"
#include <iostream>

any_websocket::any_websocket(tcp_transport&& t, boost::beast::flat_buffer&& rxbuf)
: ws_(tcp_websock(std::move(t)))
, rxbuf_(std::move(rxbuf))
, write_condition_(get_executor())
, join_condition_(get_executor())
{
    
}

any_websocket::any_websocket(tls_transport&& t, boost::beast::flat_buffer&& rxbuf)
: ws_(tls_websock(std::move(t)))
, rxbuf_(std::move(rxbuf))
, write_condition_(get_executor())
, join_condition_(get_executor())
{

}

asio::awaitable<void>
any_websocket::accept(request_type& request)
{
    auto op = [&](auto& ws)
    {
        return ws.async_accept(request, asio::use_awaitable);
    };

    co_await visit(op, ws_);
}


tcp_transport const& 
any_websocket::socket() const
{
    auto op = [](auto& ws) -> decltype(auto)
    {
        return beast::get_lowest_layer(ws);
    };

    return visit(op, ws_);
}

void 
queue_write(std::shared_ptr<any_websocket> pws, std::string s, frame_type type)
{
    asio::co_spawn(pws->get_executor(), 
        write(pws, std::move(s), type), 
        asio::detached);
}

asio::awaitable<void>
any_websocket::write(std::string s, frame_type type)
{
    // If we are not the first writer, wait on the write
    // condition variable. We are guaranteed to be woken up 
    // in order.
    if(outstanding_writes_++ != 0)
        co_await write_condition_.wait();

    // ensure that the condition is correctly notified whether
    // the write fails with an exception or completes correctly
    auto bump = [&]
    {
        --outstanding_writes_;
        write_condition_.notify_one();
        if (outstanding_writes_ == 0 && !closing_)
            join_condition_.notify_all();
    };

    auto write_op = [&s, type](auto& ws)
    {
        switch (type)
        {
        case frame_type::text:
            ws.text();
            break;
            
        case frame_type::binary:
            ws.binary();
            break;
        }
        return ws.async_write(asio::buffer(s), asio::use_awaitable);
    };

    // Wait for the write to complete before dropping out of scope
    // and updating the condition variable
    try
    {
        co_await visit(write_op, ws_);
        bump();
    }
    catch(const std::exception& e)
    {
        std::cerr << "websocket write failed: " << e.what() << '\n';
        bump();
    }
}

asio::awaitable< frame >
any_websocket::read()
{
    rxbuf_.consume(last_read_size_);
    last_read_size_ = 0;

    auto read_op = [this](auto& ws)
    {
        return ws.async_read(rxbuf_, asioex::as_tuple(asio::use_awaitable));
    };

    auto got_binary = [](auto& ws)
    {
        return ws.got_binary();
    };

    auto ec = error_code();

    std::tie(ec, last_read_size_) = 
        co_await 
            visit(read_op, ws_);

    if (ec)
    {
        co_await join();
        throw system_error(ec);
    }
    else
    {
        co_return 
            frame(rxbuf_, 
                visit(got_binary, ws_));
    }
}

asio::awaitable<void>
any_websocket::close(beast::websocket::close_reason reason)
{
    assert(!closing_);

    closing_ = true;

    auto bump =[&]
    {
        closing_ = false;
        if (outstanding_writes_ == 0 /* && !closing_ implied */)
            join_condition_.notify_all();

    };

    try
    {
        co_await
            visit([&](auto& ws)
            {
                return ws.async_close(reason, asio::use_awaitable);
            }, ws_);
            bump();
    }
    catch(std::exception& e)
    {
        std::cerr << "websocket close failed: " << e.what() << '\n';
        bump();
    }
}

asio::awaitable<void> 
any_websocket::join()
{
    auto pred = [&]
    {
        return outstanding_writes_ || closing_;
    };

    while(pred())
        co_await join_condition_.wait();
}


asio::any_io_executor
any_websocket::get_executor()
{
    auto op = [](auto& ws)
    {
        return ws.get_executor();
    };
    return visit(op, ws_);
}

// free functions

asio::awaitable<void>
write(std::shared_ptr<any_websocket> impl, std::string s, frame_type)
{
    // note - impl has been passed by value, causing a copy. Thereby guaranteeing 
    // that the lifetime of the websocket is preserved during the execution of the
    // inner coroutine
    co_await impl->write(std::move(s));
}
