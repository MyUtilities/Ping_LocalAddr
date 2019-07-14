#include "Network_Check.h"

//
// This class manages socket timeouts by applying the concept of a deadline.
// Some asynchronous operations are given deadlines by which they must complete.
// Deadlines are enforced by an "actor" that persists for the lifetime of the
// client object:
//
//  +----------------+
//  |                |
//  | check_deadline |<---+
//  |                |    |
//  +----------------+    | async_wait()
//              |         |
//              +---------+
//
// If the deadline actor determines that the deadline has expired, the socket
// is closed and any outstanding operations are consequently cancelled.
//
// Connection establishment involves trying each endpoint in turn until a
// connection is successful, or the available endpoints are exhausted. If the
// deadline actor closes the socket, the connect actor is woken up and moves to
// the next endpoint.
//
//  +---------------+
//  |               |
//  | start_connect |<---+
//  |               |    |
//  +---------------+    |
//           |           |
//  async_-  |    +----------------+
// connect() |    |                |
//           +--->| handle_connect |
//                |                |
//                +----------------+
//                          :
// Once a connection is     :
// made, the connect        :
// actor forks in two -     :
//                          :
// an actor for reading     :       and an actor for
// inbound messages:        :       sending heartbeats:
//                          :
//  +------------+          :          +-------------+
//  |            |<- - - - -+- - - - ->|             |
//  | start_read |                     | start_write |<---+
//  |            |<---+                |             |    |
//  +------------+    |                +-------------+    | async_wait()
//          |         |                        |          |
//  async_- |    +-------------+       async_- |    +--------------+
//   read_- |    |             |       write() |    |              |
//  until() +--->| handle_read |               +--->| handle_write |
//               |             |                    |              |
//               +-------------+                    +--------------+
//
// The input actor reads messages from the socket, where messages are delimited
// by the newline character. The deadline for a complete message is 30 seconds.
//
// The heartbeat actor sends a heartbeat (a message that consists of a single
// newline character) every 10 seconds. In this example, no deadline is applied
// message sending.
//
class client
{
public:
	client(boost::asio::io_service& io_service)
		: stopped_(false),
		socket_(io_service),
		deadline_(io_service),
		heartbeat_timer_(io_service)
	{
		m_timeout_ms_connect = 500;
		m_timeout_ms_read = 100;
		m_timeout_ms_write = 100;
		isConnected = false;
	}

	// Called by the user of the client class to initiate the connection process.
	// The endpoint iterator will have been obtained using a tcp::resolver.
	void start(tcp::resolver::iterator endpoint_iter)
	{
		// Start the connect actor.
		start_connect(endpoint_iter);

		// Start the deadline actor. You will note that we're not setting any
		// particular deadline here. Instead, the connect and input actors will
		// update the deadline prior to each asynchronous operation.
		deadline_.async_wait(boost::bind(&client::check_deadline, this));
	}

	// This function terminates all the actors to shut down the connection. It
	// may be called by the user of the client class, or by the class itself in
	// response to graceful termination or an unrecoverable error.
	void stop()
	{
		stopped_ = true;
		socket_.close();
		deadline_.cancel();
		heartbeat_timer_.cancel();
	}
	bool check_connected()
	{
		return isConnected;
	}
private:
	void start_connect(tcp::resolver::iterator endpoint_iter)
	{
		if (endpoint_iter != tcp::resolver::iterator())
		{
			std::cout << "Trying " << endpoint_iter->endpoint() << "...\n";

			// Set a deadline for the connect operation.
			deadline_.expires_from_now(boost::posix_time::milliseconds(m_timeout_ms_connect));

			// Start the asynchronous connect operation.
			socket_.async_connect(endpoint_iter->endpoint(),
				boost::bind(&client::handle_connect,
					this, _1, endpoint_iter));
		}
		else
		{
			// There are no more endpoints to try. Shut down the client.
			stop();
		}
	}

	void handle_connect(const boost::system::error_code& ec,
		tcp::resolver::iterator endpoint_iter)
	{
		if (stopped_)
			return;

		// The async_connect() function automatically opens the socket at the start
		// of the asynchronous operation. If the socket is closed at this time then
		// the timeout handler must have run first.
		if (!socket_.is_open())
		{
			std::cout << "Connect timed out\n";

			// Try the next available endpoint.
			start_connect(++endpoint_iter);
		}

		// Check if the connect operation failed before the deadline expired.
		else if (ec)
		{
			std::cout << "Connect error: " << ec.message() << "\n";

			// We need to close the socket used in the previous connection attempt
			// before starting a new one.
			socket_.close();

			// Try the next available endpoint.
			start_connect(++endpoint_iter);
		}

		// Otherwise we have successfully established a connection.
		else
		{
			std::cout << "===============================Connected to " << endpoint_iter->endpoint() << "\n";
			isConnected = true;
			// Start the input actor.
			start_read();

			// Start the heartbeat actor.
			start_write();
		}
	}

	void start_read()
	{
		// Set a deadline for the read operation.
		deadline_.expires_from_now(boost::posix_time::milliseconds(m_timeout_ms_read));

		// Start an asynchronous operation to read a newline-delimited message.
		boost::asio::async_read_until(socket_, input_buffer_, '\n',
			boost::bind(&client::handle_read, this, _1));
	}

	void handle_read(const boost::system::error_code& ec)
	{
		if (stopped_)
			return;

		if (!ec)
		{
			// Extract the newline-delimited message from the buffer.
			std::string line;
			std::istream is(&input_buffer_);
			std::getline(is, line);

			// Empty messages are heartbeats and so ignored.
			if (!line.empty())
			{
				std::cout << "Received: " << line << "\n";
			}

			start_read();
		}
		else
		{
			std::cout << "Error on receive: " << ec.message() << "\n";

			stop();
		}
	}

	void start_write()
	{
		if (stopped_)
			return;

		// Start an asynchronous operation to send a heartbeat message.
		boost::asio::async_write(socket_, boost::asio::buffer("\n", 1),
			boost::bind(&client::handle_write, this, _1));
	}

	void handle_write(const boost::system::error_code& ec)
	{
		if (stopped_)
			return;

		if (!ec)
		{
			// Wait 10 seconds before sending the next heartbeat.
			heartbeat_timer_.expires_from_now(boost::posix_time::milliseconds(m_timeout_ms_write));
			heartbeat_timer_.async_wait(boost::bind(&client::start_write, this));
		}
		else
		{
			std::cout << "Error on heartbeat: " << ec.message() << "\n";

			stop();
		}
	}

	void check_deadline()
	{
		if (stopped_)
			return;

		// Check whether the deadline has passed. We compare the deadline against
		// the current time since a new asynchronous operation may have moved the
		// deadline before this actor had a chance to run.
		if (deadline_.expires_at() <= deadline_timer::traits_type::now())
		{
			// The deadline has passed. The socket is closed so that any outstanding
			// asynchronous operations are cancelled.
			socket_.close();

			// There is no longer an active deadline. The expiry is set to positive
			// infinity so that the actor takes no action until a new deadline is set.
			deadline_.expires_at(boost::posix_time::pos_infin);
		}

		// Put the actor back to sleep.
		deadline_.async_wait(boost::bind(&client::check_deadline, this));
	}

	

private:
	bool stopped_;
	tcp::socket socket_;
	boost::asio::streambuf input_buffer_;
	deadline_timer deadline_;
	deadline_timer heartbeat_timer_;
private:
	int m_timeout_ms_connect;
	int m_timeout_ms_read;
	int m_timeout_ms_write;

	bool isConnected;
};
Network_Check::Network_Check()
{

}

Network_Check::~Network_Check()
{
}

bool Network_Check::Connect_With_Timeout(std::string ipaddr, std::string portnum)
{
	boost::asio::io_service io_service;
	tcp::resolver r(io_service);
	client c(io_service);
	try
	{		 
		c.start(r.resolve(tcp::resolver::query(ipaddr, portnum)));

		//boost::thread t(boost::bind(&boost::asio::io_service::run, &io_service));
		//t.join();
		io_service.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
		return false;
	}

/*	if (c.check_connected())
	{
		m_ok_address.push_back(ipaddr);
	}*/

	return c.check_connected();
}

bool Network_Check::Start_MultiThread()
{
	unsigned char local_ip[4];
	bool success = Get_GateWayIPaddr(local_ip);
	if (!success) {
		// do something
	}
	m_ok_address.clear();
	std::vector<std::string> ok_address;
	Network_Check mync[255];
	boost::thread con_thread[255];
	for (int i = 1; i < 3; i++ )
	{
		char ipaddr_buf[50];
		sprintf(ipaddr_buf, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], i);
		printf("Before Start Thread [%s]\n", ipaddr_buf);
		con_thread[i] = boost::thread(boost::bind(&Network_Check::Connect_With_Timeout, this, ipaddr_buf, "80"));
		//printf("After Start Thread [%s]\n", ipaddr_buf);
		//con_thread.join();
		//bool ConOK = Connect_With_Timeout(ipaddr_buf, "80");		
	}
	for (int i = 1; i < 255; i++)
	{
		con_thread[i].join();
	}
	printf("Available ip list \n");
	for (int i = 0; i < ok_address.size(); i++)
	{
		printf("%s \n", ok_address[i].c_str());
	}
	return true;
}

bool Network_Check::Start()
{
	unsigned char local_ip[4];
	bool success = Get_GateWayIPaddr(local_ip);
	if (!success) {
		// do something
	}

	std::vector<std::string> ok_address;
	for (int i = 1; i < 254; i++)
	{		
		char ipaddr_buf[50];
		sprintf(ipaddr_buf, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], i);		
		bool ConOK = Connect_With_Timeout(ipaddr_buf, "80");
		if (ConOK)
		{
			ok_address.push_back(ipaddr_buf);
		}		
	}
	printf("Available ip list \n");
	for (int i = 0; i < ok_address.size(); i++)
	{
		printf("%s \n", ok_address[i].c_str());
	}
	return true;
}


void Network_Check::Start(int ip, std::vector<std::string> *ok_address)
{
	unsigned char local_ip[4];
	bool success = Get_GateWayIPaddr(local_ip);
	if (!success) {
		// do something
	}	
	int i = ip;
	char ipaddr_buf[50];
	sprintf(ipaddr_buf, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], i);
	bool ConOK = Connect_With_Timeout(ipaddr_buf, "80");
	if (ConOK)
	{
		ok_address->push_back(ipaddr_buf);
	}	
}
/*

bool Network_Check::Start(int start, int end)
{
	unsigned char local_ip[4];
	bool success = Get_GateWayIPaddr(local_ip);
	if (!success) {
		// do something
	}

	std::vector<std::string> ok_address;
	for (int i = start; i <= end; i++)
	{
		char ipaddr_buf[50];
		sprintf(ipaddr_buf, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], i);
		bool ConOK = Connect_With_Timeout(ipaddr_buf, "80");
		if (ConOK)
		{
			ok_address.push_back(ipaddr_buf);
		}
	}
	printf("Available ip list \n");
	for (int i = 0; i < ok_address.size(); i++)
	{
		printf("%s \n", ok_address[i].c_str());
	}
	return true;
}*/

bool Network_Check::Get_GateWayIPaddr(unsigned char ipaddr[4])
{
	auto destination_address = boost::asio::ip::address::from_string("8.8.8.8");
	std::string myip = source_address(destination_address).to_string();
	std::cout << "Source ip address: " << myip << '\n';
	split_ip_addr(myip, ipaddr);
	// do check local ip get from dhcp
	return true;
}


void Network_Check::split_ip_addr(std::string myipaddr, unsigned char ipaddr[4])
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
boost::asio::ip::address Network_Check::source_address(const boost::asio::ip::address& ip_address)
{
	using boost::asio::ip::udp;
	boost::asio::io_service service;
	udp::socket socket(service);
	udp::endpoint endpoint(ip_address, 0);
	socket.connect(endpoint);
	return socket.local_endpoint().address();
}





void rvs_subutil::available_ipaddr_check()
{
	boost::thread con_thread[255];
	Network_Check nc[255];
	
	for (int i = 1; i <= 254; i++)
	{
		con_thread[i] = boost::thread(boost::bind(&Network_Check::Start, nc[i], i, &m_available_ipaddr_list));
		//printf("After Start Thread [%s]\n", ipaddr_buf);
		//con_thread.join();
		//bool ConOK = Connect_With_Timeout(ipaddr_buf, "80");		
	}
	for (int i = 1; i < 254; i++)
	{
		con_thread[i].join();
	}
	for (int i = 0; i < m_available_ipaddr_list.size(); i++)
	{
		printf("%s \n", m_available_ipaddr_list[i].c_str());
	}
}