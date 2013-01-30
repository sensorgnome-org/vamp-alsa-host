/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include "PollableMinder.hpp"

PollableMinder::PollableMinder():
  regen_pollfds(false),
  have_deferrals(false),
  doing_poll(false)
{
};
  
void PollableMinder::add(Pollable *p) {
  if (! doing_poll) {
    pollables.insert(p);
    regen_pollfds = true;
  } else {
    deferred_adds.insert(p);
    have_deferrals = true;
  }
}
  
void PollableMinder::remove(Pollable *p) {
  if (! doing_poll) {
    pollables.erase(p);
    regen_pollfds = true;
  } else {
    deferred_removes.insert(p);
    have_deferrals = true;
  }
}

void PollableMinder::setEvents(Pollable *p, short events, int offset = 0) {
  // set the events field for a pollable in the set
  // For Pollables with more than one FD, offset can be used
  // to select among them.
  if (first_pollable.count(p) == 0)
    return;
  pollfds[first_pollable[p] + offset].events = events;
};

void PollableMinder::requestPollFDRegen() {
  regen_pollfds = true;
};

int PollableMinder::poll(int timeout) {
  doing_poll = true;

  regenFDs();
  int rv = poll(& pollfds[0], pollfds.size(), timeout);
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
    is->handleEvents(& pollfds[i], timedOut, regen_pollfds, this);
    i += is->getNumPollFDs();
  }
  doing_poll = false;
  doDeferrals();
  return 0;
}

void PollableMinder::doDeferrals() {
  if (! have_deferrals)
    return;
  have_deferrals = false;
  regen_pollfds = true;
  for (PollableSet::iterator is = deferred_removes.begin(); is != deferred_removes.end(); ++is) 
    pollables.erase(*is);
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
    for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); ++is) {
      int where = pollfds.size();
      first_pollfd[*is] = where;
      int numFDs = is->getNumPollFDs();
      pollfds.reserve(where + numFD);
      is->getPollFDs(& pollfds[where]);
    }
  }
}
