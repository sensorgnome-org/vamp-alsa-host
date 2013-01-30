#ifndef POLLABLEMINDER_HPP
#define POLLABLEMINDER_HPP

/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include <set>
#include <vector>
#include <map>
class Pollable;

typedef std::set <Pollable *> PollableSet;

class PollableMinder {

protected:
  std::vector <struct pollfd> pollfds;
  PollableSet pollables; // maintained in same order as pollfds, but might not be of same length if some objects have more than one FD
  PollableSet deferred_adds;
  PollableSet deferred_removes;
  std::map <Pollable *, int > first_pollfd; // map of Pollable * to index of first corresponding struct pollfd in pollfds
  bool regen_pollfds;
  bool have_deferrals;
  bool doing_poll;
public:
  
  PollableMinder();
  void add(Pollable *p);
  void remove(Pollable *p);
  void setEvents(Pollable *p, short events, int offset = 0);
  void requestPollFDRegen();
  int poll(int timeout);

protected:

  void doDeferrals();  
  void regenFDs();
};
    
#endif // POLLABLEMINDER_HPP
