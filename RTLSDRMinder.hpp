#ifndef RTLSDRMINDER_HPP
#define RTLSDRMINDER_HPP

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <sys/socket.h>
#include <sys/un.h>

using namespace std;

#include "DevMinder.hpp"

class RTLSDRMinder : public DevMinder {

protected:

  int                numFD;            // number of file descriptors required for polling on this device
  int                rtltcp;           // fd for connection to rtl_tcp server via unix domain socket; -1 means not connected
  struct sockaddr_un rtltcpAddr;       // address for rtl_tcp server
  std::string        socketPath;       // filesystem path to rtl_tcp unix domain socket


public:

  const static int RTLSDR_FRAMES = 2048;
  virtual int hw_open();

  virtual bool hw_is_open();

  RTLSDRMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now);

  ~RTLSDRMinder();

  virtual int hw_getNumPollFDs ();

  virtual int hw_getPollFDs (struct pollfd *pollfds);

  virtual int hw_handleEvents ( struct pollfd *pollfds, bool timedOut);

  virtual int hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp);

protected:

  virtual void hw_delete_privates();
  virtual int hw_do_start();
  virtual int hw_do_stop();
  virtual int hw_do_restart();

};

#endif // RTLSDRMINDER_HPP
