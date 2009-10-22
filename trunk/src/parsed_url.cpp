#include "parsed_url.hpp"
#include <string.h>

parsed_url::parsed_url(const char* url, bool& succeed)
{
    succeed = set(url);
}

bool parsed_url::set(const char* url)
{
    if (strnicmp(url, "http://", 7) == 0) {
        _scheme = "http";
        _port = 80;
    } else if (strnicmp(url, "https://", 7) == 0) {
        _scheme = "https";
        _port = 443;
    } else {
        return false;
    }

	// Scan for either:
	//   /, which means the path starts
	//   NUL, which means there is no path

	_url = url;
	url += 7;

	const char* sep = strchr(url, '/');
	if (sep == 0) {
		sep = strchr(url, 0);
		_path.clear();
	}
	else
	{
		_path = sep;
	}

    // check port number
	if (char* p = (char*)memchr(url, ':', sep - url))
	{
		sep = p;
		_port = atoi(p + 1);
	}

	_host.assign(url, sep - url);
	return true;
}
