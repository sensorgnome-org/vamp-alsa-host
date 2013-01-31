#include "VAHConnection.hpp"

VAHConnection::VAHConnection (int fd)
{
  pollfd.fd = fd;
  pollfd.events = POLLIN | POLLRDHUP;
};
  
int VAHConnection::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

void VAHConnection::queueOutputBytes(const char *txt, int len) {
  // add len bytes at txt to output buffer
  if (len > MAX_OUTPUT_BUFFER_SIZE)
    len = MAX_OUTPUT_BUFFER_SIZE;
  int extra = len + outputBuff.size() - MAX_OUTPUT_BUFFER_SIZE;
  if (extra > 0)
    outputBuff.erase(0, extra);
  outputBuff.append(txt, len);
};

void VAHConnection::queueOutputString(string s) {
  if (s.length() == 0)
    return;
  queueOutputBytes(s.c_str(), s.length());
};

    
void VAHConnection::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow, PollableMinder *minder) {
  if (outputBuff.length() > 0 && ! (pollfds->events & POLLOUT)) {
    // start watching for POLLOUT, since we have stuff to write
    pollfd.events |= POLLOUT;
    pollfds->events = pollfd.events;
  }

  if (pollfds->revents & (POLLIN | POLLRDHUP)) {
    // handle read 
    int len = read(pollfd.fd, cmdString, MAX_CMD_STRING_LENGTH);
    if (len <= 0) {
      // socket has been closed, apparently
      minder->remove(this);
      return;
    }
    cmdString[len] = '\0';
    inputBuff += string(cmdString); // might be as long as 2 * MAX_CMD_STRING_LENGTH
    // look for a '\n'
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
    if (commandHandler) {
      queueOutputString(commandHandler(cmd)); // call the toplevel
      pollfds->events |= POLLOUT; // flag that there's output
    }
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

void VAHConnection::setCommandHandler (CommandHandler commandHandler) {
  VAHConnection::commandHandler = commandHandler;
};

CommandHandler VAHConnection::commandHandler = 0;
