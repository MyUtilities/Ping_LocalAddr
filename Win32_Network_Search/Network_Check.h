#pragma once



#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <iostream>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;

class Network_Check
{
public:
	Network_Check();
	~Network_Check();

private:
	bool Connect_With_Timeout(std::string ipaddr, std::string portnum);
	bool Get_GateWayIPaddr(unsigned char ipaddr[4]);
	void split_ip_addr(std::string myipaddr, unsigned char ipaddr[4]);
	boost::asio::ip::address source_address(const boost::asio::ip::address& ip_address);
public:	
	bool Start();
	void Start(int ip, std::vector<std::string> *ok_address);
	//bool Start(int start, int end);
	bool Start_MultiThread();

private:
	std::vector<std::string> m_ok_address;

};

class rvs_subutil {
public:
	rvs_subutil() {}
	~rvs_subutil() {}
	void available_ipaddr_check();
private:
	std::vector<std::string> m_available_ipaddr_list;
};