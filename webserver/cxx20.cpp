#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/deferred.hpp>
#include <boost/beast.hpp>
#include <boost/system/system_error.hpp>
#include <string_view>

namespace asio = boost::asio;
namespace asioex = boost::asio::experimental;
namespace beast = boost::beast;
using namespace std::literals;


struct read_latch;

struct latch
{
    explicit latch(asio::any_io_executor exec)
    : timer_(exec)
    {
        timer_.expires_at(asio::steady_timer::time_point::max());
    }

    void trigger()
    {
        timer_.expires_at(asio::steady_timer::time_point::min());
    }


    template <typename CompletionToken>
    auto operator()(CompletionToken&& token)
    {
        return timer_.async_wait
        (
            asioex::deferred(
                [](auto)
                {
                    return asioex::deferred.values();
                }
            )
        )
        (
            std::forward<CompletionToken>(token)
        );
    }

    bool triggered() const 
    {
        return timer_.expiry() == asio::steady_timer::time_point::min();
    }

private:
    friend read_latch;
    asio::steady_timer timer_;
};

struct read_latch
{
    explicit read_latch(latch& l)
    : timer_(&l.timer_)
    {

    }

    template <typename CompletionToken>
    auto operator()(CompletionToken&& token)
    {
        return timer_->async_wait
        (
            asioex::deferred(
                [](auto)
                {
                    return asioex::deferred.values();
                }
            )
        )
        (
            std::forward<CompletionToken>(token)
        );
    }

    bool triggered() const 
    {
        return timer_->expiry() == asio::steady_timer::time_point::min();
    }

private:

    asio::steady_timer* timer_;
};

struct program_stop
{
    program_stop(asio::any_io_executor exec)
    : latch_(exec)
    {}

    operator read_latch() {
        return read_latch(latch_);
    }

    bool stopped() const {
        return latch_.triggered();
    }

    void stop(std::exception& e)
    {
        stop(127, e.what());
    }

    void stop(int retcode, std::string msg)
    {
        if (!stopped())
        {
            code_ = retcode;
            message_ = std::move(msg);
        }
    }

    template <typename CompletionToken>
    auto operator()(CompletionToken&& token)
    {
        return latch_(std::forward<CompletionToken>(token));
    }

    int code() const { return code_; }

    std::string const& message() const { return message_; }

private:
    latch latch_;
    int code_ = 0;
    std::string message_;
};



template<class Stream, class T>
void emit(Stream& s, T&& x)
{
    s << x;
}

template<class...Idents>
void report(std::exception const& e, std::string_view context, Idents&&...idents)
{
    std::cout << context;
    if constexpr (sizeof...(idents) != 0)
    {
        auto sep = "["sv;
        (
            (
                emit(std::cout, sep),
                emit(std::cout, idents),
                sep = ", "
            ), ...
        );
        emit(std::cout, ']');
    }
    emit(std::cout, " : ");
    emit(std::cout, e.what());
    emit(std::cout, '\n');
}

asio::awaitable<bool> 
detect_ssl(asio::ip::tcp::socket& sock, beast::flat_buffer& buf)
{
    auto cstate = (co_await asio::this_coro::cancellation_state);
    cstate.slot().assign([&](asio::cancellation_type type) {
        sock.close();
    });

    std::cout << "detecting ssl\n";
    try {
        auto is_ssl = co_await beast::async_detect_ssl(sock, buf, asio::use_awaitable);
        std::cout << "is ssl: " << is_ssl << "\n";
        co_return is_ssl;
    }
    catch(std::exception& e)
    {
        std::cout << "detect_ssl: exception - " << e.what() << "\n";
        throw;
    }
}

asio::awaitable<void>
chat(asio::ip::tcp::socket sock)
{
    using namespace asioex::awaitable_operators;

    auto ident = sock.remote_endpoint();
    auto timer = asio::steady_timer(co_await asio::this_coro::executor);
    try
    {
        std::cout << "accepted: " << sock.remote_endpoint() << "\n";

        timer.expires_from_now(5s);
        auto buf = beast::flat_buffer();
        auto which = co_await 
        (
            detect_ssl(sock, buf) || 
            timer.async_wait(asio::use_awaitable)
        );

        std::cout << "chat: which = " << which.index() << "\n";
    }
    catch(std::exception& e)
    {
        report(e, "chat"sv, ident);
    }
    std::cout << "chat over\n";
    co_return;
}

asio::awaitable<void> 
listen(program_stop& pstop)
try
{
    using namespace asioex::awaitable_operators;

    std::cout << "creating acceptor\n";
    auto acceptor = asio::ip::tcp::acceptor(co_await asio::this_coro::executor);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 8080));
    acceptor.listen();
    while(!pstop.stopped())
    {
        std::cout << "accepting...\n";
        auto sock = asio::ip::tcp::socket(co_await asio::this_coro::executor);
        auto which = co_await (
            acceptor.async_accept(sock, asioex::as_tuple(asio::use_awaitable)) ||
            pstop(asio::use_awaitable)
        );

        if (which.index() == 0)
        {
            auto [ec] = std::get<0>(which);
            if (ec) {
                if (ec == asio::error::connection_aborted) continue;
                else if (ec == asio::error::operation_aborted) break;
                else throw boost::system::system_error(ec);
            }

            asio::co_spawn(co_await asio::this_coro::executor, 
                [](auto sock, auto& pstop)->asio::awaitable<void>
                {
                    co_await (
                        chat(std::move(sock)) || 
                        pstop(asio::use_awaitable));
                }(std::move(sock), pstop), 
                asio::detached);
        }
    }
    std::cout << "listen: exit\n";
}
catch(std::exception& e)
{
    std::cout << "listen: exception: " << e.what() << "\n";
    pstop.stop(e);
}

asio::awaitable<void>
monitor_sigint(program_stop& pstop)
{
    auto sigs = asio::signal_set(co_await asio::this_coro::executor, SIGINT);

    static const char* msgs[] = {
        "First interrupt. Press ctrl-c again to stop the program.\n",
        "Really?\n",
        "You asked for it!\n"
    };

    for (auto& msg : msgs)
    {
        if (pstop.stopped()) co_return;

        co_await sigs.async_wait(asio::use_awaitable);
        std::cout << msg;
    }
    pstop.stop(4, "interrupted");
}

asio::awaitable<void>
co_main(program_stop& pstop)
{
    using namespace asioex::awaitable_operators;

    co_await(listen(pstop) || monitor_sigint(pstop));
//    co_await(listen());
}

int main()
{
    std::cout << "Hello, World!\n";

    asio::io_context ioc;
    auto exec = ioc.get_executor();

    auto pstop = program_stop(exec);
    asio::co_spawn(exec, co_main(pstop), asio::detached);

    ioc.run();

    switch(pstop.code())
    {
        case 0: break;
        default:
            std::cerr << "webserver: " << pstop.message() << '\n';
    }
    return pstop.code();
}
