#ifndef POLLABLEMINDER_HPP
#define POLLABLEMINDER_HPP

/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include <set>
#include <vector>
#include <map>
#include <poll.h>
#include <errno.h>
#include <string>
#include <sstream>
#include <memory>

using std::string;
using std::istringstream;
using std::ostringstream;

class Pollable;

typedef std::set < Pollable * > PollableOwnerSet;
typedef std::set < Pollable * > PollableSet;
typedef std::map <Pollable *, int > PollableIndex;

class PollableMinder {

protected:
  PollableOwnerSet pollables; // maintained in same order as pollfds, but might not be of same length if some objects have more than one FD
  // this object owns the objects pointed to
  std::vector <struct pollfd> pollfds;
  PollableIndex first_pollfd; // map of Pollable * to index of first corresponding struct pollfd in pollfds
  PollableSet deferred_adds;
  PollableSet deferred_removes;
  bool regen_pollfds;
  bool have_deferrals;
  bool doing_poll;
public:
  
  PollableMinder();
  void add(Pollable *p);
  void remove(Pollable *p);
  short & eventsOf(Pollable *p, int offset = 0); // reference to the events field for a pollfd
  void requestPollFDRegen();
  int poll(int timeout, double (*now)(bool isRealtime));

protected:

  void doDeferrals();  
  void regenFDs();
};

#endif // POLLABLEMINDER_HPP
