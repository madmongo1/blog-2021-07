#include <boost/asio.hpp>
#include <iostream>

namespace asio = boost::asio;
using error_code = boost::system::error_code;

template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, std::size_t)) CompletionToken>
auto async_double(std::size_t input, CompletionToken&& token)
-> BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompetionToken, void(error_code, std::size_t))
{
    struct state_t
    {
        state_t(asio::any_io_executor e)
        : timer_(e)
        {

        }

        asio::steady_timer timer_;
    };

    // initiate the composed operation.
    // the composed operation will be associated with the executor associated with the token/completion handler
    // if there is no executor associated with the token, it will be associated with the system executor (Bad!)
    return
        asio::async_compose<CompletionToken, void(error_code, std::size_t)>(
            [input, state = std::unique_ptr<state_t>(), coro = asio::coroutine()]
            (auto&& self, error_code ec = {}, std::size_t = 0) 
            mutable
            {
                auto pstate = state.get();
                BOOST_ASIO_CORO_REENTER(coro)
                {
                    state.reset(new state_t(asio::get_associated_executor(self)));
                    pstate = state.get();
                    state->timer_.expires_after(std::chrono::seconds(1));
                    // 1. suspend the current coroutine
                    // 2. initiate an async_wait, passing ourselves as the completion handler
                    // 3. we will be resumed by this lambda being called again, with our arguments
                    //    set to the result of the async operation
                    BOOST_ASIO_CORO_YIELD
                        state->timer_.async_wait(std::move(self)); // note that until we resume, self is now moved-from, as is state and coro
                    // now suspended
                    
                    // resume point
                    if (ec)
                    {
                        // clean up memory before completing.
                        state.reset();
                        self.complete(ec, 0);
                        return;
                    }
                    else
                    {
                        // clean up memory before completing.
                        state.reset();
                        // no need to post a completion here because the timer has already completed as if by
                        // post()
                        self.complete(ec, input * 2);
                    }

                }
            }, token);
}

template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, std::size_t)) CompletionToken>
auto async_times_eight(std::size_t input, CompletionToken&& token)
-> BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompetionToken, void(error_code, std::size_t))
{
    // initiate the composed operation.
    // the composed operation will be associated with the executor associated with the token/completion handler
    // if there is no executor associated with the token, it will be associated with the system executor (Bad!)
    return
        asio::async_compose<CompletionToken, void(error_code, std::size_t)>(
            [input, i = int(3), coro = asio::coroutine()]
            (auto&& self, error_code ec = {}, std::size_t y_ = 0) 
            mutable
            {
                BOOST_ASIO_CORO_REENTER(coro)
                for(;;)
                {
                    BOOST_ASIO_CORO_YIELD
                        async_double(input, std::move(self));
                    if (ec) {
                        self.complete(ec, 0);
                        return;
                    } else {
                        input = y_;
                        std::cout << "by eight: input'=" << input << std::endl;
                        if (--i == 0)
                        {
                            self.complete(ec, input);
                            return;
                        }
                    }
                }
            }, token);
}



int main()
{
    auto ioc = asio::io_context();
    auto exec = ioc.get_executor();

    // demo with bound completion handler:
    async_times_eight(2, asio::bind_executor(exec, [](error_code ec, std::size_t y)
    {
        if (ec)
        {
            std::cout << "times eight failed: " << ec << std::endl;
        }
        else
        {
            std::cout << "y = " << y << std::endl;
        }
    }));

    // demo with coroutine
    asio::co_spawn(exec, []()->asio::awaitable<void>{
        auto y = co_await async_times_eight(3, asio::use_awaitable);
        std::cout << "3 times eight = " << y << std::endl;

    }, asio::detached);


    ioc.run();
}