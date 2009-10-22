#ifndef CONTROL_HPP__
#define CONTROL_HPP__

#include <string>
#include "xplat_hash_map.hpp"
#include <boost/lexical_cast.hpp>
#include "request.hpp"
#include "utils.hpp"

USING_NAMESPACE_EXT

class ControlAPI {
public:
    typedef boost::function< void(std::vector< std::string >) > void_vector_str_f_t;
    typedef boost::function< std::string(void) > string_void_f_t;

    typedef hash_map< std::string, std::vector<std::string> > arg_map;

    ControlAPI() { }
    bool add_variable(const std::string &name, void_vector_str_f_t writer, string_void_f_t reader);
    int process(const boost::asio::ip::tcp::endpoint& endpoint,
            const http::server::request &req, const arg_map &params);
    std::string dump();
    bool read_file(const std::string& configfile);
    static void set_bool(bool *p, std::vector< std::string > args);
    static void set_int(int *p, std::vector< std::string > args);
    static std::string get_bool(bool *p);
    static std::string get_int(int *p);
    static void set_string(std::string *p, std::vector< std::string > args);
    static std::string get_string(std::string *p);
private:
    struct VarFuncs {
        void_vector_str_f_t set;
        string_void_f_t get;
    };
    typedef hash_map< std::string, VarFuncs > var_map;
    var_map vars;
};

#endif /* CONTROL_HPP__ */
