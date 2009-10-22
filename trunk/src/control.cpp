/*
The MIT License

Copyright (c) 2009 BitTorrent Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <fstream>

#include "connection.hpp"
#include "control.hpp"

#include <string>
#include "xplat_hash_map.hpp"
#include <boost/lexical_cast.hpp>

#include <boost/asio.hpp>

int ControlAPI::process(const boost::asio::ip::tcp::endpoint &endpoint,
        const http::server::request &req, const ControlAPI::arg_map &params)
{
    int ret = 0, err = 0;

    logger << "control: " << endpoint << " " << req.method << " " << req.uri << std::endl;
    for(
            ControlAPI::arg_map::const_iterator it = params.begin();
            it != params.end();
            it++)
    {
        if (verbose_logging)
            logger << it->first << " = " << it->second[0] << std::endl;
        if (vars.count(it->first) > 0)
        {
            vars[it->first].set(it->second);
            ret++;
        }
        else
        {
            err++;
        }
    }

    if (err > 0)
        return 0;
    else
        return ret;
}

bool ControlAPI::add_variable(const std::string &name,
        void_vector_str_f_t writer, string_void_f_t reader)
{
    VarFuncs vf = { writer, reader };
    vars[name] = vf;
    return true;
}

void ControlAPI::set_bool(bool *p, std::vector< std::string > args)
{
    if (args[0] == "true" || args[0] == "1")
    {
        *p = true;
    }
    else if (args[0] == "false" || args[0] == "0")
    {
        *p = false;
    }
    else
    {
        throw std::runtime_error("not a valid boolean value");
    }
}

void ControlAPI::set_int(int *p, std::vector< std::string > args)
{
    *p = boost::lexical_cast<int>(args[0]);
}

std::string ControlAPI::get_bool(bool *p)
{
    if (*p)
    {
        return "true";
    }
    else
    {
        return "false";
    }
}

std::string ControlAPI::get_int(int *p)
{
    std::stringstream os;

    os << *p;
    return os.str();
}

void ControlAPI::set_string(std::string *p, std::vector< std::string > args)
{
    *p = args[0];
}

std::string ControlAPI::get_string(std::string *p)
{
    return *p;
}

std::string ControlAPI::dump()
{
    std::stringstream os;
	std::vector<std::string> keys;

    for (var_map::const_iterator it = vars.begin(); it != vars.end(); it++)
    {
        keys.push_back(it->first);
    }

    sort(keys.begin(), keys.end());

	for (std::vector<std::string>::const_iterator ki = keys.begin(); ki != keys.end(); ki++)
    {
        os << *ki << ": " << vars[*ki].get() << std::endl;
    }

    return os.str();
}

bool ControlAPI::read_file(const std::string& configfile)
{
    std::string line;
    const char *whitespace = " \t";
    int errors = 0;
    int lineno = 0;

    std::ifstream f(configfile.c_str());

    if (!f)
    {
        int e = errno;
        logger << configfile << ": " << strerror(e) << std::endl;
        return false;
    }

    for (getline(f, line), lineno++; !f.eof(); getline(f, line), lineno++) {
        std::string::size_type pos = line.find_first_not_of(whitespace);

        if (pos == std::string::npos || line[pos] == '#')
            continue;

        /* get rid of trailing comment, if any */
        pos = line.find("#");
        if (pos != std::string::npos)
        {
            line.resize(pos);
        }

        std::string::size_type end_key = line.find(":");
        if (end_key != std::string::npos)
            pos = line.find_first_not_of(whitespace, end_key+1);

        if (pos == std::string::npos || pos+1 > line.size())
        {
            logger << configfile << ":" << lineno << ": Missing value in '" <<
                line << "'\n";
            errors++;
            continue;
        }

        std::string key(line, 0, end_key), val(line, pos);

        if (vars.count(key) > 0)
        {
            std::vector<std::string> v;
            v.push_back(val);
            try
            {
                logger << key << ": " << val << "\n";
                vars[key].set(v);
            }
            catch (const std::runtime_error &e)
            {
                logger << configfile << ":" << lineno << ": " << e.what() <<
                    ": '" << val << "' for '" << key << "'\n";
                errors++;
                continue;
            }
        }
        else
        {
            logger << configfile << ":" << lineno << ": unknown variable '" <<
                key << "'\n";
            errors++;
            continue;
        }
    }
    return errors > 0 ? false : true;
}
