#include "TCPConnection.hpp"
#include <iomanip>

string TCPConnection::toJSON() {
  ostringstream s;
  s << "{"
    << "\"type\":\"TCPConnection\""
    << ",\"fileDescriptor\":" << pollfd.fd
    << ",\"timeConnected\":" << std::setprecision(14) << timeConnected
    << "}";
  return s.str();
};

TCPConnection::TCPConnection (int fd, string label, CommandHandler handler, bool quiet, double timeNow) :
  Pollable(label),
  handler(handler),
  timeConnected(timeNow)
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
  outputBuffer = boost::circular_buffer < char > (RAW_OUTPUT_BUFFER_SIZE);
  if (! quiet)
    queueOutput(msg);
};

TCPConnection::~TCPConnection ()
{
  close (pollfd.fd);
  pollfd.fd = -1;
};

int TCPConnection::getNumPollFDs() {
  return 1;
};

int TCPConnection::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

int TCPConnection::getOutputFD() {
  return pollfd.fd;
};

void TCPConnection::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {

  if (pollfds->revents & (POLLERR | POLLHUP | POLLNVAL)) {
    remove(label);
    requestPollFDRegen();
    return;
  }

  if (pollfds->revents & (POLLIN | POLLRDHUP)) {
    // handle read
    int len = read(pollfd.fd, cmdString, VampAlsaHost::MAX_CMD_STRING_LENGTH);
    if (len <= 0) {
      // socket has been closed, apparently.

      // Remove this object from the pollables set.

      // This will call its destructor, which as of 2015-08-18, closes
      // the fd.  Fixes longstanding issue of not closing
      // TCPConnection fds, which was exposed by the catastrophic
      // fcd-watchdog software update of 28 July 2015.

      remove(label);
      requestPollFDRegen();
      return;
    }
    cmdString[len] = '\0';
    inputBuff += string(cmdString); // might be as long as 2 * MAX_CMD_STRING_LENGTH
    // handle as many '\n'-terminated command lines as we can find
    for (;;) {
      size_t pos = inputBuff.find('\n');
      if (pos == inputBuff.npos) {
        // none found; prevent input buffer from growing too big
        if (inputBuff.npos > VampAlsaHost::MAX_CMD_STRING_LENGTH)
          inputBuff.erase(0, inputBuff.npos - VampAlsaHost::MAX_CMD_STRING_LENGTH);
        return;
      }

      // grab the full command
      string cmd(inputBuff.substr(0, pos));
      // drop it from the input buffer; note that since the newline is in the
      // last MAX_CMD_STRING_LENGTH characters of inputBuff, removing the command
      // will keep that buffer's length <= MAX_CMD_STRING_LENGTH
      inputBuff.erase(0, pos + 1);
      string rv = (*handler)(cmd, label);
      queueOutput(rv); // call the command handler
    }
  }

  if (pollfds->revents & (POLLOUT)) {
    writeSomeOutput(outputBuffer.size());
  }
};

void TCPConnection::stop(double timeNow) {
  outputPaused = true;
};

int TCPConnection::start(double timeNow) {
  /* do nothing */
  outputPaused = false;
  return 0;
};

void TCPConnection::setRawOutput(bool yesno) {
  unsigned capacity = yesno ? TCPConnection::RAW_OUTPUT_BUFFER_SIZE : Pollable::DEFAULT_OUTPUT_BUFFER_SIZE;
  if( capacity != outputBuffer.capacity())
    outputBuffer = boost::circular_buffer < char > (capacity);
};
