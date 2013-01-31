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

void VAHConnection::queueFloatOutput(std::vector < float > & f) {
  outputFloatBuffer.insert(outputFloatBuffer.end(), f.begin(), f.end());
};

void VAHConnection::queueTextOutput(string& s) {
  if (s.length() == 0)
    return;
  outputLineBuffer.push_back(s);
};

    
void VAHConnection::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow, PollableMinder *minder) {

  // set whether or not to watch for POLLOUT
  // kludgey to do it here, but this has access to the PollableMinder's
  // set of pollfds, so we can set the events field there.

  bool haveOutput = outputFloatBuffer.size() > 0 || outputLineBuffer.size() > 0 || outputPartialLine.length() > 0;
  if (haveOutput) {
    pollfd.events |= POLLOUT;
  } else {
    pollfd.events &= ~POLLOUT;
  }
  pollfds->events = pollfd.events;

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
    if (commandHandler)
      queueOutputString(commandHandler(cmd)); // call the toplevel
  }

  if (pollfds->revents & (POLLOUT)) {
    // handle writeable:
    // if there's a partial line, finish sending that first
    // if there are any text lines, send as much as possible of the first line
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
            minder->remove(this);
          }
        } else {
          outputPartialLine.erase(0, num_bytes);
        }
        return;
      }
      if (outputBufferedLines.empty())
        break;
      outputPartialLine = outputBufferedLines.front();
      outputBufferedLines.pop_front();
    }
    
    int len = outputFloatBuffer.length() * sizeof(float);
    if (len > 0) {     
      // write only from the first array; a subsequent call to this handler can write data
      // which is now in the second array but which will eventually be in the first array.
      array_range aone = outputFloatBuffer.array_one();
      int num_bytes = write(pollfd.fd, (char *) aone.first, aone.second * sizeof(float));
      if (num_bytes == len) {
        outputFloatBuffer.clear();
      } else {
        int partial = num_bytes % sizeof(float);
        if (partial != 0) {
          // PITA: wrote part of a float; we at least want to prevent these from being broken up
          outputPartialLine = string( ((char *) aone.first) + num_bytes, sizeof(float) - part);
        }
        outputFloatBuffer.erase_begin((num_bytes + sizeof(float) - 1) / sizeof(float)); // round up to number of floats at least partially written
      }
    }
  }
};

void VAHConnection::setCommandHandler (CommandHandler commandHandler) {
  VAHConnection::commandHandler = commandHandler;
};

CommandHandler VAHConnection::commandHandler = 0;
