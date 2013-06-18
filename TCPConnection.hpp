#ifndef TCPCONNECTION_HPP
#define TCPCONNECTION_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"
#include "VampAlsaHost.hpp"

class TCPConnection;

class TCPConnection : public Pollable {

public:

  string toJSON();

  TCPConnection (int fd, string label, CommandHandler handler, bool quiet, double timeNow);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  int getOutputFD();
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  void setRawOutput(bool yesno);

protected: 
  CommandHandler handler;
  static const unsigned RAW_OUTPUT_BUFFER_SIZE = 256;    // size of buffer for receiving commands over TCP

  char cmdString[VampAlsaHost::MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet

  std::weak_ptr < Pollable > outputListener;
  double timeConnected;

};

#endif // TCPCONNECTION_HPP
