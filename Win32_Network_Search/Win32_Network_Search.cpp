#if 0
// Win32_Network_Search.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <boost/thread.hpp>
#include <boost/asio/serial_port.hpp> 
#include <boost/asio.hpp> 
#include <boost/bind.hpp>
#include <istream>
#include <iostream>
#include <ostream>
#include "HttpClient.h"
#include "icmp_header.hpp"
#include "ipv4_header.hpp"
#include "Interface_Tcp.h"
using boost::asio::ip::tcp;
using boost::asio::ip::icmp;
using boost::asio::deadline_timer;
using namespace std;
using namespace boost;

namespace posix_time = boost::posix_time;

class pinger
{
public:
	pinger(boost::asio::io_service& io_service, const char* destination, int timeout_ms, vector<string> *success_address_list)
		: resolver_(io_service), socket_(io_service, icmp::v4()),
		timer_(io_service), sequence_number_(0), num_replies_(0), m_timeout_ms(timeout_ms), m_success_address_list(success_address_list), m_destip(destination)
	{
		icmp::resolver::query query(icmp::v4(), destination, "");
		destination_ = *resolver_.resolve(query);

		start_send();
		start_receive();
	}

private:

	void start_send()
	{
		std::string body("1");

		// Create an ICMP header for an echo request.
		icmp_header echo_request;
		echo_request.type(icmp_header::echo_request);
		echo_request.code(0);
		echo_request.identifier(get_identifier());
		echo_request.sequence_number(++sequence_number_);
		compute_checksum(echo_request, body.begin(), body.end());

		// Encode the request packet.
		boost::asio::streambuf request_buffer;
		std::ostream os(&request_buffer);
		os << echo_request << body;

		// Send the request.
		time_sent_ = boost::posix_time::microsec_clock::universal_time();
		socket_.send_to(request_buffer.data(), destination_);

		// Wait up to five seconds for a reply.
		num_replies_ = 0;
		timer_.expires_at(time_sent_ + boost::posix_time::millisec(m_timeout_ms));
		timer_.async_wait(boost::bind(&pinger::handle_timeout, this));
	}

	void handle_timeout()
	{
		if (num_replies_ == 0)
		{
			printf("[%s] can't establish. \n", m_destip.c_str());
		}	
		socket_.close();
	}

	void start_receive()
	{
		// Discard any data already in the buffer.
		reply_buffer_.consume(reply_buffer_.size());

		// Wait for a reply. We prepare the buffer to receive up to 64KB.
		socket_.async_receive(reply_buffer_.prepare(65536),
			boost::bind(&pinger::handle_receive, this, _2));
	}

	void handle_receive(std::size_t length)
	{
		// The actual number of bytes received is committed to the buffer so that we
		// can extract it using a std::istream object.
		reply_buffer_.commit(length);

		// Decode the reply packet.
		std::istream is(&reply_buffer_);
		ipv4_header ipv4_hdr;
		icmp_header icmp_hdr;
		is >> ipv4_hdr >> icmp_hdr;

		// We can receive all ICMP packets received by the host, so we need to
		// filter out only the echo replies that match the our identifier and
		// expected sequence number.
		if (is && icmp_hdr.type() == icmp_header::echo_reply
			&& icmp_hdr.identifier() == get_identifier()
			&& icmp_hdr.sequence_number() == sequence_number_)
		{
			// If this is the first reply, interrupt the five second timeout.
			if (num_replies_++ == 0)
				timer_.cancel();

			// Print out some information about the reply packet.
			boost::posix_time::ptime now = boost::posix_time::microsec_clock::universal_time();
			std::cout << length - ipv4_hdr.header_length()
				<< " bytes from " << ipv4_hdr.source_address()
				<< ": icmp_seq=" << icmp_hdr.sequence_number()
				<< ", ttl=" << ipv4_hdr.time_to_live()
				<< ", time=" << (now - time_sent_).total_milliseconds() << " ms"
				<< std::endl;
			m_success_address_list->push_back(ipv4_hdr.source_address().to_string());
		}
		socket_.close();
	}

	static unsigned short get_identifier()
	{
#if defined(BOOST_WINDOWS)
		return static_cast<unsigned short>(::GetCurrentProcessId());
#else
		return static_cast<unsigned short>(::getpid());
#endif
	}
	int m_timeout_ms;
	string m_destip;
	vector<string> *m_success_address_list;
	icmp::resolver resolver_;
	icmp::endpoint destination_;
	icmp::socket socket_;
	deadline_timer timer_;
	unsigned short sequence_number_;
	boost::posix_time::ptime time_sent_;
	boost::asio::streambuf reply_buffer_;
	std::size_t num_replies_;
};

void split_ip_addr(string myipaddr, unsigned char ipaddr[4])
{
	auto ipp = boost::asio::ip::address_v4::from_string(myipaddr);
//	std::cout << "to_string()  " << ipp.to_string() << std::endl;
//	std::cout << "to_ulong()  " << ipp.to_ulong() << std::endl;
	auto bt(ipp.to_bytes());
	std::cout << "to_bytes()  " << (int)bt[0] << "." << (int)bt[1] << "." << (int)bt[2] << "." << (int)bt[3] << std::endl;
	ipaddr[0] = bt[0];
	ipaddr[1] = bt[1];
	ipaddr[2] = bt[2];
	ipaddr[3] = bt[3];
}
boost::asio::ip::address source_address(
	const boost::asio::ip::address& ip_address) {
	using boost::asio::ip::udp;
	boost::asio::io_service service;
	udp::socket socket(service);
	udp::endpoint endpoint(ip_address, 0);
	socket.connect(endpoint);
	return socket.local_endpoint().address();
}



int main(int argc, char* argv[])
{

	auto destination_address = boost::asio::ip::address::from_string("8.8.8.8");
	string myip = source_address(destination_address).to_string();
	std::cout << "Source ip address: " << myip << '\n';

	unsigned char ipaddr[4];
	split_ip_addr(myip, ipaddr);

	
	vector<string> ok_address;
	for (int i = 1; i < 254; i++)
	{
		try
		{
			char ipaddr_buf[50];
			sprintf(ipaddr_buf, "%d.%d.%d.%d", ipaddr[0], ipaddr[1], ipaddr[2], i);
			boost::asio::io_service io_service;
			//printf("[%s] TEST BEFORE\n",ipaddr_buf);
			pinger p(io_service, ipaddr_buf, 100, &ok_address);
			io_service.run();
			//printf("[%s] TEST AFTER\n",ipaddr_buf);
		}
		catch (std::exception& e)
		{
			std::cerr << "Exception: " << e.what() << std::endl;
		}
	}

	for (int i = 0; i < ok_address.size(); i++)
	{
		printf("OK IP [%s]\n", ok_address[i].c_str());
	}
	
#if 0
	std::wcout.imbue(std::locale(""));

	AsioHttp::Client client;
	client.Init("192.168.10.20", CP_UTF8);

	//std::wcout << client.Request("//cgi-bin/ensemble.cgi").strData << std::endl;

	printf("==============start============\n");

	wstring req_bufw = client.Request("//cgi-bin/ensemble.cgi").strData;
	string server_reply;
	server_reply.assign(req_bufw.begin(), req_bufw.end());

	printf(" server reply \n");
	printf("%s\n", server_reply.c_str());
	printf("===============end==============\n");
#endif
}

#endif
//
// async_tcp_client.cpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2011 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "Network_Check.h"
int main(int argc, char* argv[])
{
	rvs_subutil rvs;
	rvs.available_ipaddr_check();
}