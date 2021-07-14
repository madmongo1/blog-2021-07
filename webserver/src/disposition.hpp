#ifndef WEBSERVER_DISPOSITION_HPP
#define WEBSERVER_DISPOSITION_HPP

#include "asio.hpp"

enum wait_disposition_t {
    success,
    cancelled,
    error
};

wait_disposition_t
disposition(error_code const& ec)
{
    if (ec) 
        if (ec == asio::error::operation_aborted)
            return cancelled;
        else
            return error;
    else
        return success;
}


#endif

