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
  
  int getNumPollFDs();

  int getPollFDs (struct pollfd * pollfds);

  int getOutputFD();
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  void setRawOutput(bool yesno);

  static const int RAW_OUTPUT_BUFFER_SIZE = 524288;    // size of buffer for sending raw output over TCP; 
  // needs to be large enough for a full buffer of output audio data (131072 frames x 2 channels x 2 bytes per channel)

protected: 
  CommandHandler handler;


  char cmdString[VampAlsaHost::MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet

  weak_ptr < Pollable > outputListener;
  double timeConnected;

};

#endif // TCPCONNECTION_HPP
