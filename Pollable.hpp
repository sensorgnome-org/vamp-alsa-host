#ifndef POLLABLE_HPP
#define POLLABLE_HPP

/* a quasi-interface to allow an object to participate in polling by a VampAlsaHost */

#include <string>
#include <stdexcept>

#include "VampAlsaHost.hpp"

class Pollable {
public:
  string label;
  VampAlsaHost *host;
  virtual string toJSON() = 0;
  Pollable(VampAlsaHost *host, string & label) : label(label), host(host), indexInPollFD(-1) {};
  virtual int getNumPollFDs() = 0;                      // return number of fds used by this Pollable (negative means error)
  virtual int getPollFDs (struct pollfd * pollfds) = 0; // copy pollfds for this Pollable to the location specified (return non-zero on error)
  // (i.e. this reports pollable fds and the pollfd "events" field for this object)
  virtual void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) = 0; // handle possible event(s) on the fds for this Pollable
  virtual int start(double timeNow) = 0;
  virtual void stop(double timeNow) = 0;
  int indexInPollFD;  // index of first FD in host's pollfd vector (< 0 means not in pollfd vector)
};

#endif /* POLLABLE_HPP */
