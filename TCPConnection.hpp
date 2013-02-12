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
  static const unsigned MAX_OUTPUT_FLOAT_BUFFER_SIZE = 8192;  // maximum size of float output buffer = 2K floats
  static const unsigned MAX_OUTPUT_LINE_BUFFER_SIZE = 128; // max # of text lines buffered for output
  static const unsigned MAX_OUTPUT_RAW_BUFFER_SIZE = 32768; // maximum size of raw frame buffer in bytes; = 8k stereo 16-bit frames

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  boost::circular_buffer < string > outputLineBuffer;  // output text lines waiting to be written back to socket
  boost::circular_buffer < float > outputFloatBuffer;  // output float data waiting to be written back to socket
  boost::circular_buffer < char > outputRawBuffer;  // output raw data waiting to be written back to socket
  int outputRawBufferGranularity; // granularity of raw output, in bytes; each chunk is either sent or not; no partial chunks are sent.

  string outputPartialLine; // if only part of an output chunk has been sent on the connection, this holds the rest

  unsigned long long rawBytesSent; // keep track of count of raw bytes, so user can know when to send a header

public:

  TCPConnection (int fd, VampAlsaHost *minder, string label);
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds);

  void queueRawOutput(const char *p, int len, int granularity);

  void queueFloatOutput(std::vector < float > & f);

  void queueTextOutput(string s);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  string toJSON();

  unsigned long long getRawBytesSent();

  void resetRawBytesSent();
};

#endif // TCPCONNECTION_HPP
