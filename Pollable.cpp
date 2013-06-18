#include "Pollable.hpp"
#include <stdint.h>

Pollable::Pollable(std::string& label) : 
  label(label), 
  outputBuffer(DEFAULT_OUTPUT_BUFFER_SIZE)
{
  pollables[label] = std::shared_ptr < Pollable > (this);
};
  
void 
Pollable::remove(const std::string& label) {
  auto p = pollables[label];
  auto pp = p.get();
  if (!pp)
    return;
  if (! doing_poll) {
    pollables.erase(pp->label);
    regen_pollfds = true;
  } else {
    deferred_removes[pp->label] = p;
    have_deferrals = true;
  }
};

Pollable * 
Pollable::lookupByName (const std::string& label) {
  if (pollables.count(label) == 0)
    return 0;
  return pollables[label].get();
};

std::shared_ptr < Pollable > 
Pollable::lookupByNameShared (const std::string& label) {
  if (pollables.count(label) == 0)
    return std::shared_ptr < Pollable > ((Pollable *) 0);
  return pollables[label];
};

short& 
Pollable::eventsOf(int offset) {

  // return a reference to the events field for a Pollable.
  // For Pollables with more than one FD, offset can be used
  // to select among them.
  return pollfds[indexInPollFD + offset].events;
};

void 
Pollable::requestPollFDRegen() {
  regen_pollfds = true;
};

int 
Pollable::poll(int timeout) {
  doing_poll = true;

  regenFDs();
  int rv = ::poll(& pollfds[0], pollfds.size(), timeout);
  if (rv < 0) {
    doing_poll = false;
    std::cerr << "poll returned error - vamp-alsa-host" << std::endl;
    return errno;
  }

  bool timedOut = rv == 0;
  // handle events for each pollable.  We give each pollable the chance 
  // to deal with timeouts, by passing that along.

  for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); ++is) {
    auto ptr = is->second.get();
    if (!ptr)
      continue;
    int i = ptr->indexInPollFD;
    if (i < 0)
      continue;
    ptr->handleEvents(&pollfds[i], timedOut, VampAlsaHost::now());
  }
  doing_poll = false;
  doDeferrals();
  return 0;
};

void 
Pollable::doDeferrals() {
  if (! have_deferrals)
    return;
  have_deferrals = false;
  regen_pollfds = true;
  for (PollableSet::iterator is = deferred_removes.begin(); is != deferred_removes.end(); ++is) 
    pollables.erase(is->first);
  deferred_removes.clear();
};
    
void 
Pollable::regenFDs() {
  if (regen_pollfds) {
    regen_pollfds = false;
    pollfds.clear();
    for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); /**/) {
      if (auto ptr = is->second.get()) {
        if (!ptr)
          continue;
        int where = pollfds.size();
        int numFDs = ptr->getNumPollFDs();
        if (numFDs > 0) {
          ptr->indexInPollFD = where;
          pollfds.resize(where + numFDs);
          ptr->getPollFDs(& pollfds[where]);
        } else {
          ptr->indexInPollFD = -1;
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

bool
Pollable::queueOutput(const char *p, uint32_t len, void * meta) {
  if ((unsigned) len > outputBuffer.capacity())
    return false;

  outputBuffer.insert(outputBuffer.end(), p, p + len);
  pollfd.events |= POLLOUT;
  if (indexInPollFD >= 0)
    eventsOf(0) = pollfd.events;

  return true;

};

int
Pollable::writeSomeOutput (int maxBytes) {
  // assuming the output FD is ready for non-blocking output, (i.e. POLLOUT true)
  // write up to maxBytes to it from the output buffer.  Return the number of
  // bytes written.  Negative return values indicate an error.

  int len = outputBuffer.size();
  if (len > 0) {
    int toWrite = std::min(maxBytes, len);
    // write only from the first array; a subsequent call to this handler can write data
    // which is now in the second array but which will eventually be in the first array.
    boost::circular_buffer < char > ::array_range aone = outputBuffer.array_one();
    toWrite = std::min(toWrite, (int)aone.second);
    int num_bytes = write(pollfd.fd, (char *) aone.first, aone.second);
    if (num_bytes < 0) {
      // error writing, call the error callback
      pollfd.events &= ~POLLOUT;
      if (indexInPollFD >= 0)
        eventsOf(0) = pollfd.events;
      return num_bytes;
    } else if (num_bytes > 0) {
      outputBuffer.erase_begin(num_bytes);
    }
    return num_bytes;
  } else {
    // output buffer is empty; stop writing 
    pollfd.events &= ~POLLOUT;
    if (indexInPollFD >= 0)
      eventsOf(0) = pollfd.events;
    return 0;
  }
};

// static initializers
std::vector < struct pollfd > Pollable::pollfds(5);
PollableSet Pollable::pollables;
PollableSet Pollable::deferred_removes;
bool Pollable::regen_pollfds = true;
bool Pollable::have_deferrals = false;
bool Pollable::doing_poll = false;

