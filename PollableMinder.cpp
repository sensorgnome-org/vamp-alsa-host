/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include "PollableMinder.hpp"
#include "Pollable.hpp"

PollableMinder::PollableMinder():
  regen_pollfds(false),
  have_deferrals(false),
  doing_poll(false)
{
};
  
void PollableMinder::add(std::shared_ptr < Pollable > p) {
  if (! doing_poll) {
    pollables[p.get()] = p;
    regen_pollfds = true;
  } else {
    deferred_adds[p.get()] = p;
    have_deferrals = true;
  }
}
  
void PollableMinder::remove(std::shared_ptr < Pollable > p) {
  if (! doing_poll) {
    pollables.erase(p.get());
    delete p.get();
    regen_pollfds = true;
  } else {
    deferred_removes[p.get()] = p;
    have_deferrals = true;
  }
}

void PollableMinder::remove(std::weak_ptr < Pollable > p) {
  remove(p.lock());
};

short& PollableMinder::eventsOf(Pollable *p, int offset) {

  // set the events field for a pollable in the set
  // For Pollables with more than one FD, offset can be used
  // to select among them.
  if (first_pollfd.count(p) == 0)
    throw std::runtime_error("PollableMinder::eventsOf: No such pollable in minder.\n");
  return pollfds[first_pollfd[p] + offset].events;
};

void PollableMinder::requestPollFDRegen() {
  regen_pollfds = true;
};

int PollableMinder::poll(int timeout, double (*now)(bool isRealtime)) {
  doing_poll = true;

  regenFDs();
  int rv = ::poll(& pollfds[0], pollfds.size(), timeout);
  if (rv < 0) {
    doing_poll = false;
    return errno;
  }

  bool timedOut = rv == 0;
  // handle events for each pollable.  We give each pollable the chance 
  // to deal with timeouts, by passing that along.
  int i = 0;
  PollableSet to_delete;
  for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); ++is) {
    auto ptr = is->second.lock();
    if (!ptr) {
      remove(is->second); // will be deferred
    } else {
      PollableIndex::iterator ip = first_pollfd.find(is->first);
      if (ip == first_pollfd.end())
        continue;
      ptr->handleEvents(&pollfds[ip->second], timedOut, (*now)(false));
    }
  }
  doing_poll = false;
  doDeferrals();
  return 0;
};

void PollableMinder::doDeferrals() {
  if (! have_deferrals)
    return;
  have_deferrals = false;
  regen_pollfds = true;
  for (PollableSet::iterator is = deferred_removes.begin(); is != deferred_removes.end(); ++is) 
    pollables.erase(is->first);
  deferred_removes.clear();
  for (PollableSet::iterator is = deferred_adds.begin(); is != deferred_adds.end(); ++is) 
    pollables.insert(*is);
  deferred_adds.clear();
};
    
void PollableMinder::regenFDs() {
  if (regen_pollfds) {
    regen_pollfds = false;
    pollfds.clear();
    first_pollfd.clear();
    for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); /**/) {
      if (auto ptr = is->second.lock()) {
        int where = pollfds.size();
        int numFDs = ptr->getNumPollFDs();
        if (numFDs > 0) {
          first_pollfd[is->first] = where;
          pollfds.resize(where + numFDs);
          ptr->getPollFDs(& pollfds[where]);
        }
        ++is;
      } else {
        auto to_delete = is;
        ++is;
        pollables.erase(to_delete->first);
      }
    }
  }
}

