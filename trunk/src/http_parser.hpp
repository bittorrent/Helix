/*
    From libtorrent, Copyright (c) 2003, Arvid Norberg
*/

#ifndef __HTTP_PARSER_HPP__
#define __HTTP_PARSER_HPP__

#include <string>
#include <map>
#include <libtorrent/buffer.hpp>
#include <boost/tuple/tuple.hpp>

using namespace libtorrent;

class http_parser
{
public:
	http_parser();
	template <class T>
	T header(char const* key) const;
	std::string const& protocol() const { return m_protocol; }
	int status_code() const { return m_status_code; }
	std::string message() const { return m_server_message; }
	buffer::const_interval get_body() const;
	bool header_finished() const { return m_state == read_body; }
	bool finished() const { return m_finished; }
	boost::tuple<int, int> incoming(buffer::const_interval recv_buffer);
	int body_start() const { return m_body_start_pos; }
	int content_length() const { return m_content_length; }

	void reset();
private:
	int m_recv_pos;
	int m_status_code;
	std::string m_protocol;
	std::string m_server_message;

	int m_content_length;

	enum { read_status, read_header, read_body } m_state;

	std::map<std::string, std::string> m_header;
	buffer::const_interval m_recv_buffer;
	int m_body_start_pos;

	bool m_finished;
};

template <class T>
T http_parser::header(char const* key) const
{
	std::map<std::string, std::string>::const_iterator i
		= m_header.find(key);
	if (i == m_header.end()) return T();
	return boost::lexical_cast<T>(i->second);
}

#endif //__HTTP_PARSER_HPP__
