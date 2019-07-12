#include "Interface_Tcp.h"

boost::asio::io_service g_io_service_none;

Interface_Tcp::Interface_Tcp(boost::asio::io_service& io_service)
	:m_ExtSocket(io_service)
{
	m_CB_Read = NULL;
	m_Socket = &m_ExtSocket;
	m_isCon_OK = true;
}


Interface_Tcp::Interface_Tcp()
	:m_ExtSocket(g_io_service_none)
{
	//	m_serial_port = NULL;
	m_Socket = NULL;
	m_io_service = NULL;
	m_isCon_OK = false;
}



Interface_Tcp::~Interface_Tcp()
{
	while (m_Socket != NULL)
	{
		boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
	}
}

tcp::socket& Interface_Tcp::Get_Socket()
{
	return *m_Socket;
}

int Interface_Tcp::Write(char *Out_Buffer, int Length)
{
	Write_Start(Out_Buffer, Length);
	return 1;
}

int Interface_Tcp::ReadLen(char *In_Buffer, int Length)
{
	int ReadNums = 0;
	boost::lock_guard<boost::mutex> lock(m_mutex);
	{
		for (int i = 0; i < Length; i++)
		{
			if (!m_RcvBuffer.empty())
			{
				ReadNums++;
				char read_data = m_RcvBuffer.front();
				m_RcvBuffer.pop_front();
				In_Buffer[i] = read_data;
			}
			else
			{
				break;
			}
		}
	}
	return ReadNums;
}

void Interface_Tcp::Read_Start()
{
	//printf("Read_Start!\n");
	try {
		if (m_Socket != NULL)
		{
			m_Socket->async_read_some(boost::asio::buffer(m_Buffer_Read, MAX_BUFFER_READ),
				boost::bind(&Interface_Tcp::Read_Handler,
					this,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred));
		}
		else
		{
			printf("Read Error!\n");
			m_isCon_OK = false;
		}
	}
	catch (boost::exception &e)
	{
		printf("Read Exception!\n");
		m_isCon_OK = false;
	}

}

void Interface_Tcp::Read_Handler(const boost::system::error_code& error, size_t bytes_transferred)
{
	if (!error)
	{
		//printf("READ HANDLER!!!\n");		
		if (m_CB_Read != NULL)
		{
			m_CB_Read(m_Buffer_Read, bytes_transferred);
		}
		boost::lock_guard<boost::mutex> lock(m_mutex);
		{
			for (int i = 0; i < bytes_transferred; i++)
			{
				m_RcvBuffer.push_back(m_Buffer_Read[i]);
			}
		}
		memset(m_Buffer_Read, 0, MAX_BUFFER_READ);
		Read_Start();
	}
	else
	{
		if (error == boost::asio::error::eof)
		{
			printf("[TCP Read Fail]Connection is broken\n");
			if (m_Socket != NULL)
			{
				m_Socket->close();
				m_isCon_OK = false;
			}
		}
		//delete this;
		// do somethings...
	}
}

void Interface_Tcp::Write_Start(char *Out_Buffer, int Length)
{
	try {
		if (m_Socket != NULL)
		{
			boost::asio::async_write(*m_Socket,
				boost::asio::buffer(Out_Buffer, Length),
				boost::bind(&Interface_Tcp::Write_Handler,
					this,
					boost::asio::placeholders::error));
		}
		else
		{
			printf("Write Fail!!\n");
			m_isCon_OK = false;
		}
	}
	catch (boost::exception &e)
	{
		printf("Write Exception!! conection broken\n");
		m_isCon_OK = false;
	}
}

void Interface_Tcp::Write_Handler(const boost::system::error_code& error)
{
	if (!error)
	{
		//printf("TCP Write Success!!!!!!!\n");
	}
	else
	{
		if (error == boost::asio::error::eof)
		{
			printf("[TCP Write Fail]Connection is broken\n");
			if (m_Socket != NULL)
			{
				m_Socket->close();
				m_isCon_OK = false;
			}
		}
		//		delete this;
	}
}

void Interface_Tcp::Connect_Start(tcp::resolver::iterator endpoint_iterator)
{
	tcp::endpoint endpoint = *endpoint_iterator;
	if (m_Socket != NULL) m_Socket->async_connect(endpoint,
		boost::bind(&Interface_Tcp::Connect_Handler,
			this,
			boost::asio::placeholders::error,
			++endpoint_iterator));
}
void Interface_Tcp::Connect_Handler(const boost::system::error_code& error, tcp::resolver::iterator endpoint_iterator)
{
	if (!error) // success, so start waiting for read data 
	{
		m_isCon_OK = true;
		Read_Start();
	}
	else if (endpoint_iterator != tcp::resolver::iterator())
	{ // failed, so wait for another connection event 
		if (m_Socket != NULL)
		{
			m_Socket->close();
			m_isCon_OK = false;
		}
		//Connect_Start(endpoint_iterator);
	}
}

bool Interface_Tcp::Connect_Accept_Wait(int milliseconds)
{
	for (int i = 0; i < milliseconds; i++)
	{
		if (isAlive() == true)
		{
			return true;
		}
		else
		{
			boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
		}
	}
	return false;
}

void Interface_Tcp::Thread_Func()
{
	asio::io_service io_service;
	m_io_service = &io_service;
	tcp::resolver resolver(io_service);
	//		tcp::resolver::query query(ipaddr, portnum);
	tcp::resolver::query query(m_Ipaddr.c_str(), m_Portnum.c_str());
	tcp::resolver::iterator iterator = resolver.resolve(query);

	//m_Socket = tcp::socket socket();
	tcp::socket mysocket(io_service);
	m_Socket = &mysocket;
	Connect_Start(iterator);

	boost::thread t(boost::bind(&boost::asio::io_service::run, &io_service));
	while (1)
	{
		boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
		if (io_service.stopped() == true)
		{
			printf("IO Service Stopped!\n");
			m_isCon_OK = false;
			m_Socket = NULL;
			break;
		}
		//m_Telnet->write_buf((char*)text, len);
	}
	m_isCon_OK = false;
}

bool Interface_Tcp::Start(char* ipaddr, char* portnum)
{
	// on Unix POXIS based systems, turn off line buffering of input, so cin.get() returns after every keypress 
// On other systems, you'll need to look for an equivalent 
	try
	{
		m_Ipaddr = std::string(ipaddr);
		m_Portnum = std::string(portnum);
		m_Thread = new boost::thread(boost::bind(&Interface_Tcp::Thread_Func, this));
		// run the IO service as a separate thread, so the main thread can block on standard input 
		bool connection_accept = Connect_Accept_Wait(100);
		if (connection_accept == false)
		{
			printf("Server is not response!\n");
		}
		return connection_accept;
	}
	catch (boost::exception& e)
	{
		cerr << "Exception: " << "\n";
		//cerr << "Exception: " << e.what() << "\n";
		return false;
	}
	return true;
}

void Interface_Tcp::Stop()
{
	if (isAlive() == true)
	{
		if (m_Socket != NULL) m_Socket->close();
	}
	while (m_Socket != NULL)
	{
		boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
	}
	m_isCon_OK = false;
}

bool Interface_Tcp::isAlive()
{
	return m_isCon_OK;
}