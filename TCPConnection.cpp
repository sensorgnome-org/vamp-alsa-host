#include "TCPConnection.hpp"

TCPConnection::TCPConnection (int fd, VampAlsaHost * host, string label, bool quiet) : 
  Pollable(host, label)
{
  static string msg ( "{"
    "\"message\":\"Welcome to vamp_alsa_host.  Type 'help' for help.\","
    "\"version\":\"1.0.0\","
    "\"author\":\"Copyright (C) 2012-2013 John Brzustowski\","
    "\"maintainer\":\"jbrzusto@fastmail.fm\","
    "\"licence\":\"GNU GPL version 2.0 or later\""
                      "}\n");

  pollfd.fd = fd;
  pollfd.events = POLLIN | POLLRDHUP;
  if (! quiet)
    queueOutput(msg);
};
  
int TCPConnection::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

bool TCPConnection::queueOutput(const char *p, uint_32 len, double lastTimestamp) {
  if (! outputListener.queueOutput(p, len, lastTimestamp))
    return false;

  pollfd.events |= POLLOUT;
  if (indexInPollFD >= 0)
    host->eventsOf(this) = pollfd.events;
  return true;
};

bool TCPConnection::queueOutput(const std::string s) {
  return queueOutput(s.data(), s.length());
};

void TCPConnection::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {

  if (pollfds->revents & (POLLERR | POLLHUP | POLLNVAL)) {
    host->remove(label);
    host->requestPollFDRegen();
    return;
  }

  if (pollfds->revents & (POLLIN | POLLRDHUP)) {
    // handle read 
    int len = read(pollfd.fd, cmdString, MAX_CMD_STRING_LENGTH);
    if (len <= 0) {
      host->remove(label);
      host->requestPollFDRegen();
      // socket has been closed, apparently
      // FIXME: delete this connection via shared_ptr in connections
      return;
    }
    cmdString[len] = '\0';
    inputBuff += string(cmdString); // might be as long as 2 * MAX_CMD_STRING_LENGTH
    // handle as many '\n'-terminated command lines as we can find
    for (;;) {
      size_t pos = inputBuff.find('\n');
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
      queueOutput(host->runCommand(cmd, label)); // call the toplevel
    }
  }

  if (pollfds->revents & (POLLOUT)) {
  }
};

void TCPConnection::setOutputWaiting(bool yesno) {
  if (yesno) {
    pollfd.events |= POLLOUT;
    if (indexInPollFD >= 0)
      host->eventsOf(this) = pollfd.events;
  } else {
      host->eventsOf(this) &= ~POLLOUT;
  }
}

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
