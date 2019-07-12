
#include <boost/thread.hpp>
#include <boost/asio/serial_port.hpp> 
#include <boost/asio.hpp> 
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include <deque>
#include <iostream>
#include <vector>
#include <cstdlib>


using boost::asio::ip::tcp;
using namespace std;
using namespace boost;

/* function list
1. write data( buf, length )
2. read data( outbuf, length )
3. use deque buffer */

class Interface_Base {
private:

public:
	Interface_Base() {}
	virtual ~Interface_Base() {}

	virtual bool Start(char* comport, int baudrate) = 0;
	virtual bool Start(char* ipaddr, char* portnum) = 0;
	virtual void Stop() = 0;

	virtual int Write(char *Out_Buffer);
	virtual int Write(char *Out_Buffer, int Length) = 0;
	virtual int ReadLen(char *In_Buffer, int Length) = 0;
	virtual int ReadAvailable() {
		return m_RcvBuffer.size();
	}
	virtual bool isAlive() = 0;

protected:
	boost::mutex m_mutex;
	std::deque<char> m_RcvBuffer;
	std::deque<char*> m_SendBuffer;
	boost::thread* m_Thread;

	virtual void Thread_Func() = 0;
	virtual void Read_Start() = 0;
	virtual void Read_Handler(const boost::system::error_code& error, size_t bytes_transferred) = 0;
	virtual void Write_Start(char *Out_Buffer, int Length) = 0;
	virtual void Write_Handler(const boost::system::error_code& error) = 0;



};
