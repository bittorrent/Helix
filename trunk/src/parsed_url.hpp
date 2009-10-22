#ifndef __PARSED_URL_H__
#define __PARSED_URL_H__

#include <string>
#include "templates.h"

struct parsed_url
{
public:
    parsed_url() : _port(0) {}
    parsed_url(const char* url, bool& succeed);
    bool set(const char* url);
    const char* scheme() const { return _scheme.c_str(); }
    const char* host() const { return _host.c_str(); }
    const char* path() const { return _path.c_str(); }
    const char* url() const { return _url.c_str(); }
    int port() const { return _port; }
private:
    std::string _scheme;
    std::string _host;
    std::string _path;
    std::string _url;
    int _port;
};


#endif //__PARSED_URL_H__
