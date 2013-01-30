#ifndef VAHCONNECTION_HPP
#define VAHCONNECTION_HPP

class VAHConnection : public Pollable {

protected: 
  struct pollfd pollfd;
  static const unsigned MAX_CMD_STRING_LENGTH = 256;    // size of buffer for receiving commands over TCP
  static const unsigned MAX_OUTPUT_BUFFER_SIZE = 8192;  // maximum size of output buffer

  char cmdString[MAX_CMD_STRING_LENGTH + 1];    // buffer for input from TCP
  string inputBuff;   // input from TCP socket which has not been processed yet
  string outputBuff;  // output waiting to be written back to socket

public:

  VAHConnection (int fd) {
    pollfd.fd = fd;
    pollfd.events = POLLIN | POLLRDHUP;
  };
  
  int getNumPollFDs() { return 1;};

  int getPollFDs (struct pollfd * pollfds) {
    * pollfds = pollfd;
    return 0;
  };

  void queueOutputBytes(char *txt, int len) {
    // add len bytes at txt to output buffer
    if (len > MAX_OUTPUT_BUFFER_SIZE)
      len = MAX_OUTPUT_BUFFER_SIZE;
    int extra = len + outputBuff.size() - MAX_OUTPUT_BUFFER_SIZE;
    if (extra > 0)
      outputBuff.erase(0, extra);
    outputBuff.append(txt, len);
  };

  void queueOutputString(string s) {
    if (s.length() == 0)
      return;
    queueOutputBytes(s.c_str(), s.length());
  };

    
  void handleEvent (struct pollfd *pollfds, bool timedOut, &regenFDs, PollableMinder *minder) {
    if (outputBuff.length() > 0 && ! (pollfds->events & POLLOUT)) {
      // start watching for POLLOUT, since we have stuff to write
      pollfd.events |= POLLOUT;
      pollfds->events = pollfd.events;
    }

    if (pollfds->revents & (POLLIN | POLLRDHUP)) {
      // handle read 
      int len = read(connectionPollfd.fd, cmdString, MAX_CMD_STRING_LENGTH);
      if (len <= 0) {
        // socket has been closed, apparently
        minder->remove(this);
        return;
      }
      cmdString[len] = '\0';
      tcpInput += string(cmdString); // might be as long as 2 * MAX_CMD_STRING_LENGTH
      // look for a '\n'
      unsigned pos = tcpInput.find('\n');
      if (pos == tcpInput.npos) {
        // none found; prevent input buffer from growing too big
        if (tcpInput.npos > MAX_CMD_STRING_LENGTH)
          tcpInput.erase(0, tcpInput.npos - MAX_CMD_STRING_LENGTH);
        return;
      }
      // grab the full command
      istringstream cmd(tcpInput.substr(0, pos));
      // drop it from the input buffer; note that since the newline is in the
      // last MAX_CMD_STRING_LENGTH characters of tcpInput, removing the command
      // will keep that buffer's length <= MAX_CMD_STRING_LENGTH
      tcpInput.erase(0, pos + 1);
      queueOutputString(runCommand(cmd)); // call the toplevel
      pollfds->events |= POLLOUT; // flag that there's output
    }

    if (pollfds->revents & (POLLOUT)) {
      // handle writeable
      if (outputBuff.length() > 0) {
        int num_bytes = write(pollfd.fd, outputBuff.c_str(), outputBuff.length());
        if (num_bytes < 0 ) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            minder->remove(this);
            return;
          }
        } else {
          outputBuff.erase(0, num_bytes);
        }
      }
      if (outputBuff.length() == 0) {
        pollfd.events &= ~ POLLOUT;
        pollfds->events = pollfd.events;
      }
    };
  };

};

#endif // VAHCONNECTION_HPP
