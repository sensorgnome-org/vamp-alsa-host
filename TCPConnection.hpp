#ifndef TCPCONNECTION_HPP
#define TCPCONNECTION_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"

class TCPConnection;

// type that handles commands
typedef string (*CommandHandler) (string cmd, string connLabel);

class TCPConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP
  static const unsigned MAX_OUTPUT_BUFFER_SIZE = 2097152; // maximum size of raw frame buffer in bytes; ~ 11 seconds at 48k in 16-bit stereo

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  boost::circular_buffer < char > outputBuffer;  // output data waiting to be written back to socket

  string outputPartialLine; // if only part of an output chunk has been sent on the connection, this holds the rest

public:

  TCPConnection (int fd, VampAlsaHost *minder, string label, bool quiet);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  bool queueOutput(const char *p, int len);

  bool queueOutput(const std::string);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  string toJSON();
};

#endif // TCPCONNECTION_HPP
