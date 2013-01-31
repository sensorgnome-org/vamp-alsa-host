#ifndef POLLABLE_HPP
#define POLLABLE_HPP

/* an interface to allow an object to participate in polling */

#include "PollableMinder.hpp"

class Pollable {
public:
    virtual int getNumPollFDs() = 0;                      // return number of fds used by this Pollable (negative means error)
    virtual int getPollFDs (struct pollfd * pollfds) = 0; // copy pollfds for this Pollable to the location specified (return non-zero on error)
                                                      // (i.e. this reports pollable fds and the pollfd "events" field for this object)
  virtual void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow, PollableMinder *minder) = 0; // handle possible event(s) on the fds for this Pollable
};

#endif /* POLLABLE_HPP */
