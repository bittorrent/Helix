//
// request.cpp
// ~~~~~~~~~~~
//
// by Steven Hazel
//

#include "request.hpp"

namespace http {
namespace server {

request::request()
{
    this->reset();
}

void request::reset()
{
    method = "";
    uri = "";
    http_version_major = 0;
    http_version_minor = 0;
    headers.clear();
}

} // namespace server
} // namespace http
