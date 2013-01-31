#ifndef VAHCONNECTION_HPP
#define VAHCONNECTION_HPP

#include <string>
#include <sstream>
using std::string;
using std::istringstream;

#include "Pollable.hpp"

// type that handles commands
typedef string (*CommandHandler) (string);

class VAHConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP
  static const unsigned MAX_OUTPUT_BUFFER_SIZE = 8192;  // maximum size of output buffer

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  string outputBuff;  // output waiting to be written back to socket
  static CommandHandler commandHandler;
  
public:

  VAHConnection (int fd);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  void queueOutputBytes(const char *txt, int len);

  void queueOutputString(string s);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow, PollableMinder *minder);

  static void setCommandHandler (CommandHandler commandHandler);
};

#endif // VAHCONNECTION_HPP
