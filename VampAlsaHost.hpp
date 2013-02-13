#ifndef VAMPALSAHOST_HPP
#define VAMPALSAHOST_HPP

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

typedef std::map < std::string, std::shared_ptr < Pollable > > PollableSet;

class VampAlsaHost {

protected:
  PollableSet pollables; // map of Pollables, indexed by label, values are shared pointers; this owns its objects
  string defaultOutputListener; // label of connection which will be automatically added as an outputListener to any new attached plugin 
  std::vector <struct pollfd> pollfds; // in same order as pollables, but some pollables may have 0 or more than 1 FD
  PollableSet deferred_adds;
  PollableSet deferred_removes;
  bool regen_pollfds;
  bool have_deferrals;
  bool doing_poll;

public:
  VampAlsaHost();
  void add(std::shared_ptr < Pollable > p);
  void remove(std::shared_ptr < Pollable > p);
  void remove(std::weak_ptr < Pollable > p);
  void remove(string& label);
  void remove(Pollable * p);
  Pollable * lookupByName (std::string& label);
  std::shared_ptr < Pollable > lookupByNameShared (std::string& label);
  short & eventsOf(Pollable *p, int offset = 0); // reference to the events field for a pollfd
  void requestPollFDRegen();
  void poll(int timeout);
  string runCommand(string cmdString, string connLabel);
  void run();
  double now(bool is_monotonic = true);
  static const string commandHelp;

protected:
  void doDeferrals();  
  void regenFDs();
};

#endif // VAMPALSAHOST_HPP
