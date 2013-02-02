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
typedef string (*CommandHandler) (string, TCPConnection *);

class TCPConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP
  static const unsigned MAX_OUTPUT_FLOAT_BUFFER_SIZE = 8192;  // maximum size of binary output buffer = 2K floats
  static const unsigned MAX_OUTPUT_LINE_BUFFER_SIZE = 128; // max # of text lines buffered for output
  static const unsigned MAX_OUTPUT_RAW_BUFFER_SIZE = 8192; // maximum size of raw frame buffer in bytes; = 2k stereo 16-bit frames

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  boost::circular_buffer < string > outputLineBuffer;  // output text lines waiting to be written back to socket
  boost::circular_buffer < float > outputFloatBuffer;  // output float data waiting to be written back to socket
  boost::circular_buffer < char > outputRawBuffer;  // output raw data waiting to be written back to socket
  int outputRawBufferGranularity; // granularity of raw output, in bytes; each chunk is either sent or not, but no partial chunks are sent.

  string outputPartialLine; // if a text line has been partially sent on the connection, this holds the rest
  static CommandHandler commandHandler;

public:

  TCPConnection (int fd, PollableMinder *minder);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  void queueRawOutput(const char *p, int len, int granularity);

  void queueFloatOutput(std::vector < float > & f);

  void queueTextOutput(string s);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  static void setCommandHandler (CommandHandler commandHandler);
};

#endif // TCPCONNECTION_HPP
