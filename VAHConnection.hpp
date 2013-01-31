#ifndef VAHCONNECTION_HPP
#define VAHCONNECTION_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"

class VAHConnection;

// type that handles commands
typedef string (*CommandHandler) (string, VAHConnection *);

class VAHConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP
  static const unsigned MAX_OUTPUT_BINARY_BUFFER_SIZE = 8192;  // maximum size of binary output buffer
  static const unsigned MAX_OUTPUT_BUFFERRED_LINES = 128; // max # of text lines buffered for output

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  boost::circular_buffer < string > outputLineBuffer;  // output text lines waiting to be written back to socket
  boost::circular_buffer < float > outputFloatBuffer;  // output binary data waiting to be written back to socket
  string outputPartialLine; // if a text line has been partially sent on the connection, this holds the rest
  static CommandHandler commandHandler;
  
public:

  VAHConnection (int fd);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  void queueFloatOutput(std::vector < float > & f);

  void queueTextOutput(string& s);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow, PollableMinder *minder);

  static void setCommandHandler (CommandHandler commandHandler);
};

#endif // VAHCONNECTION_HPP
