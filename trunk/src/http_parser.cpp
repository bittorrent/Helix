/*
    From libtorrent, Copyright (c) 2003, Arvid Norberg
*/

#include <boost/lexical_cast.hpp>
#include "http_parser.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "templates.h"
#include "utils.hpp"

typedef uint64 size_type;

char to_lower(char c) { return std::tolower(c); }

http_parser::http_parser()
	: m_recv_pos(0)
	, m_status_code(-1)
	, m_content_length(-1)
	, m_state(read_status)
	, m_recv_buffer(0, 0)
	, m_body_start_pos(0)
	, m_finished(false)
{}

boost::tuple<int, int> http_parser::incoming(buffer::const_interval recv_buffer)
{
	conn_assert(recv_buffer.left() >= m_recv_buffer.left());
	boost::tuple<int, int> ret(0, 0);

	// early exit if there's nothing new in the receive buffer
	if (recv_buffer.left() == m_recv_buffer.left()) return ret;
	m_recv_buffer = recv_buffer;

	char const* pos = recv_buffer.begin + m_recv_pos;
	if (m_state == read_status)
	{
		conn_assert(!m_finished);
		char const* newline = std::find(pos, recv_buffer.end, '\n');
		// if we don't have a full line yet, wait.
		if (newline == recv_buffer.end) return ret;

		if (newline == pos)
			throw std::runtime_error("unexpected newline in HTTP response");

		char const* line_end = newline;
		if (pos != line_end && *(line_end - 1) == '\r') --line_end;

		std::istringstream line(std::string(pos, line_end));
		++newline;
		int incoming = (int)std::distance(pos, newline);
		m_recv_pos += incoming;
		boost::get<1>(ret) += incoming;
		pos = newline;

		line >> m_protocol;
		if (m_protocol.substr(0, 5) != "HTTP/")
		{
			throw std::runtime_error("unknown protocol in HTTP response: "
				+ m_protocol + " line: " + std::string(pos, newline));
		}
		line >> m_status_code;
		std::getline(line, m_server_message);
		m_state = read_header;
	}

	if (m_state == read_header)
	{
		conn_assert(!m_finished);
		char const* newline = std::find(pos, recv_buffer.end, '\n');
		std::string line;

		while (newline != recv_buffer.end && m_state == read_header)
		{
			// if the LF character is preceeded by a CR
			// charachter, don't copy it into the line string.
			char const* line_end = newline;
			if (pos != line_end && *(line_end - 1) == '\r') --line_end;
			line.assign(pos, line_end);
			++newline;
			m_recv_pos += newline - pos;
			boost::get<1>(ret) += newline - pos;
			pos = newline;

			std::string::size_type separator = line.find(':');
			if (separator == std::string::npos)
			{
				// this means we got a blank line,
				// the header is finished and the body
				// starts.
				m_state = read_body;
				m_body_start_pos = m_recv_pos;
				break;
			}

			std::string name = line.substr(0, separator);
            std::transform(name.begin(), name.end(), name.begin(), &to_lower);
			++separator;
			// skip whitespace
			while (separator < line.size()
				&& (line[separator] == ' ' || line[separator] == '\t'))
				++separator;
			std::string value = line.substr(separator, std::string::npos);
			m_header.insert(std::make_pair(name, value));

			if (name == "content-length")
			{
				try
				{
					m_content_length = boost::lexical_cast<int>(value);
				}
				catch(boost::bad_lexical_cast&) {}
			}
			else if (name == "content-range")
			{
				std::stringstream range_str(value);
				char dummy;
				std::string bytes;
				size_type range_start, range_end;
				range_str >> bytes >> range_start >> dummy >> range_end;
				if (!range_str || range_end < range_start)
				{
					throw std::runtime_error("invalid content-range in HTTP response: " + range_str.str());
				}
				// the http range is inclusive
				m_content_length = range_end - range_start + 1;
			}

			conn_assert(m_recv_pos <= (int)recv_buffer.left());
			newline = std::find(pos, recv_buffer.end, '\n');
		}
	}

	if (m_state == read_body)
	{
		int incoming = recv_buffer.end - pos;
		if (m_recv_pos - m_body_start_pos + incoming > m_content_length
			&& m_content_length >= 0)
			incoming = m_content_length - m_recv_pos + m_body_start_pos;

		conn_assert(incoming >= 0);
		m_recv_pos += incoming;
		boost::get<0>(ret) += incoming;

		if (m_content_length >= 0
			&& m_recv_pos - m_body_start_pos >= m_content_length)
		{
			m_finished = true;
		}
	}
	return ret;
}

buffer::const_interval http_parser::get_body() const
{
	conn_assert(m_state == read_body);
	if (m_content_length >= 0)
		return buffer::const_interval(m_recv_buffer.begin + m_body_start_pos
			, m_recv_buffer.begin + (std::min)(m_recv_pos
			, m_body_start_pos + m_content_length));
	else
		return buffer::const_interval(m_recv_buffer.begin + m_body_start_pos
			, m_recv_buffer.begin + m_recv_pos);
}

void http_parser::reset()
{
	m_recv_pos = 0;
	m_body_start_pos = 0;
	m_status_code = -1;
	m_content_length = -1;
	m_finished = false;
	m_state = read_status;
	m_recv_buffer.begin = 0;
	m_recv_buffer.end = 0;
	m_header.clear();
}

