#include "TCPConnection.hpp"

TCPConnection::TCPConnection (int fd, VampAlsaHost * host, string label) : 
  Pollable(host, label),
  outputLineBuffer(MAX_OUTPUT_LINE_BUFFER_SIZE),
  outputFloatBuffer(MAX_OUTPUT_FLOAT_BUFFER_SIZE),
  outputRawBuffer(MAX_OUTPUT_RAW_BUFFER_SIZE)
{
  pollfd.fd = fd;
  pollfd.events = POLLIN | POLLRDHUP;
  queueTextOutput("{"
                  "\"message\":\"Welcome to vamp_alsa_host.  Type 'help' for help.\","
                  "\"version\":\"1.0.0\","
                  "\"author\":\"Copyright (C) 2012-2013 John Brzustowski\","
                  "\"maintainer\":\"jbrzusto@fastmail.fm\","
                  "\"licence\":\"GNU GPL version 2.0 or later\""
                  "}\n");
};
  
int TCPConnection::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

void TCPConnection::queueFloatOutput(std::vector < float > & f) {
  int len = std::min((int) f.size(), (int) (outputFloatBuffer.capacity() / sizeof(float)));
  
  outputFloatBuffer.insert(outputFloatBuffer.end(), f.begin(), f.begin() + len);
  pollfd.events |= POLLOUT;
  if (indexInPollFD >= 0)
    host->eventsOf(this) = pollfd.events;
};

void TCPConnection::queueTextOutput(string s) {
  if (s.length() == 0)
    return;
  outputLineBuffer.push_back(s);
  pollfd.events |= POLLOUT;
  if (indexInPollFD >= 0)
    host->eventsOf(this) = pollfd.events;
};

void TCPConnection::queueRawOutput(const char *p, int len, int granularity) {
  len = std::min(len, (int) outputRawBuffer.capacity());
  len = len - len % granularity;
  outputRawBuffer.insert(outputRawBuffer.end(), p, p+len);
  outputRawBufferGranularity = granularity;
  pollfd.events |= POLLOUT;
  if (indexInPollFD >= 0)
    host->eventsOf(this) = pollfd.events;
};
    
void TCPConnection::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {

  // set whether or not to watch for POLLOUT
  // kludgey to do it here, but this has access to the VampAlsaHost's
  // set of pollfds, so we can set the events field there.

  if (pollfds->revents & (POLLIN | POLLRDHUP)) {
    // handle read 
    int len = read(pollfd.fd, cmdString, MAX_CMD_STRING_LENGTH);
    if (len <= 0) {
      host->remove(label);
      // socket has been closed, apparently
      // FIXME: delete this connection via shared_ptr in connections
      return;
    }
    cmdString[len] = '\0';
    inputBuff += string(cmdString); // might be as long as 2 * MAX_CMD_STRING_LENGTH
    // handle as many '\n'-terminated command lines as we can find
    for (;;) {
      unsigned pos = inputBuff.find('\n');
      if (pos == inputBuff.npos) {
        // none found; prevent input buffer from growing too big
        if (inputBuff.npos > MAX_CMD_STRING_LENGTH)
          inputBuff.erase(0, inputBuff.npos - MAX_CMD_STRING_LENGTH);
        return;
      }
      
      // grab the full command
      string cmd(inputBuff.substr(0, pos));
      // drop it from the input buffer; note that since the newline is in the
      // last MAX_CMD_STRING_LENGTH characters of inputBuff, removing the command
      // will keep that buffer's length <= MAX_CMD_STRING_LENGTH
      inputBuff.erase(0, pos + 1);
      queueTextOutput(host->runCommand(cmd, label)); // call the toplevel
    }
  }

  if (pollfds->revents & (POLLOUT)) {
    // handle writeable:
    // if there's a partial line, finish sending that first
    // if there are any text lines, send as much as possible of the first line
    // if there's raw output to send, send as much as possible
    // if there is anything in the float output buffer, send as much as possible,
    // but don't break up within a float.
    // Not very efficient for sending text lines, since this handler must
    // be called for each line, and then once again to notice there are no
    // lines left to be processed, but text output is expected to be
    // low-bandwidth.  For high-bandwidth float output, this is
    // reasonably efficient.

    for (;;) { // loop on text lines to output
      int len = outputPartialLine.length();
      if (len > 0) {     
        int num_bytes = write(pollfd.fd, outputPartialLine.c_str(), len);
        if (num_bytes < 0 ) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
      // FIXME: delete this connection via shared_ptr in connections
          }
        } else {
          outputPartialLine.erase(0, num_bytes);
        }
        return;
      }
      if (outputLineBuffer.empty())
        break;
      outputPartialLine = outputLineBuffer.front();
      outputLineBuffer.pop_front();
    }

    int len = outputRawBuffer.size();
    if (len > 0) {
      // write only from the first array; a subsequent call to this handler can write data
      // which is now in the second array but which will eventually be in the first array.
      boost::circular_buffer < char > ::array_range aone = outputRawBuffer.array_one();
      int num_bytes = write(pollfd.fd, (char *) aone.first, aone.second);
      if (num_bytes == len) {
        outputRawBuffer.clear();
      } else {
        int partial = num_bytes % outputRawBufferGranularity;
        if (partial != 0) {
          // PITA: wrote part of a frame; we at least want to prevent these from being broken up
          outputPartialLine = string( ((char *) aone.first) + num_bytes, outputRawBufferGranularity - partial);
        }
        outputRawBuffer.erase_begin((num_bytes + outputRawBufferGranularity - 1) / outputRawBufferGranularity * outputRawBufferGranularity); // round up to number of floats at least partially written
      }
    }

    len = outputFloatBuffer.size() * sizeof(float);
    if (len > 0) {
      // write only from the first array; a subsequent call to this handler can write data
      // which is now in the second array but which will eventually be in the first array.
      boost::circular_buffer < float > ::array_range aone = outputFloatBuffer.array_one();
      int num_bytes = write(pollfd.fd, (char *) aone.first, aone.second * sizeof(float));
      if (num_bytes == len) {
        outputFloatBuffer.clear();
      } else {
        int partial = num_bytes % sizeof(float);
        if (partial != 0) {
          // PITA: wrote part of a float; we at least want to prevent these from being broken up
          outputPartialLine = string( ((char *) aone.first) + num_bytes, sizeof(float) - partial);
        }
        outputFloatBuffer.erase_begin((num_bytes + sizeof(float) - 1) / sizeof(float)); // round up to number of floats at least partially written
      }
    } else {
      host->eventsOf(this) &= ~POLLOUT;
    }
  }
};

void TCPConnection::stop(double timeNow) {
  /* do nothing */
};

int TCPConnection::start(double timeNow) {
  /* do nothing */
  return 0;
};

string TCPConnection::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"type\":\"TCPConnection\","
    << "\"fileDescriptor\":" << pollfd.fd
    << "}";
  return s.str();
};
