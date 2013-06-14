#ifndef TCPCONNECTION_HPP
#define TCPCONNECTION_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"
#include "RawListener.hpp"

class TCPConnection;

// type that handles commands
typedef string (*CommandHandler) (string cmd, string connLabel);

class TCPConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet

  RawListener outputListener;

public:

  TCPConnection (int fd, VampAlsaHost *minder, string label, bool quiet);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  bool queueOutput(const char *p, int len, double lastTimestamp=0);

  bool queueOutput(const std::string);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  string toJSON();
};

#endif // TCPCONNECTION_HPP
