#include "Pollable.hpp"
#include <stdint.h>

Pollable::Pollable(const std::string& label) : 
  label(label), 
  indexInPollFD(-1),
  outputBuffer(DEFAULT_OUTPUT_BUFFER_SIZE)
{
  pollfd.fd = -1;
  pollables[label] = shared_ptr < Pollable > (this);
  regen_pollfds = true;
};
  
Pollable::~Pollable() {
  //  std::cout << "About to destroy Pollable with label " << label << std::endl;
};

void 
Pollable::remove(const std::string& label) {
  if (! pollables.count(label))
    return;
  shared_ptr < Pollable > p = pollables[label];
  Pollable * pp = p.get();
  if (!pp) {
    pollables.erase(label);
    return;
  }
  if (! doing_poll) {
    pollables.erase(label);
    regen_pollfds = true;
  } else {
    deferred_removes.push_back(label);
    have_deferrals = true;
  }
  if (label == controlSocketLabel)
    controlSocketClosed();
};

Pollable * 
Pollable::lookupByName (const std::string& label) {
  if (pollables.count(label) == 0)
    return 0;
  return pollables[label].get();
};

shared_ptr < Pollable > 
Pollable::lookupByNameShared (const std::string& label) {
  if (pollables.count(label) == 0)
    return shared_ptr < Pollable > ((Pollable *) 0);
  return pollables[label];
};

short& 
Pollable::eventsOf(int offset) {

  // return a reference to the events field for a Pollable.
  // For Pollables with more than one FD, offset can be used
  // to select among them.
  return allpollfds[indexInPollFD + offset].events;
};

void 
Pollable::requestPollFDRegen() {
  regen_pollfds = true;
};

int 
Pollable::poll(int timeout) {
  doing_poll = true;

  regenFDs();
  int rv = ::poll(& allpollfds[0], allpollfds.size(), timeout);
  if (rv < 0) {
    doing_poll = false;
    //    std::cerr << "poll returned error - vamp-alsa-host" << std::endl;
    return errno;
  }

  bool timedOut = rv == 0;
  // handle events for each pollable.  We give each pollable the chance 
  // to deal with timeouts, by passing that along.

  for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); ++is) {
    Pollable * ptr = is->second.get();
    if (!ptr)
      continue;
    int i = ptr->indexInPollFD;
    if (i < 0)
      continue;
    ptr->handleEvents(&allpollfds[i], timedOut, VampAlsaHost::now());
    if (regen_pollfds)
      break;
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
  for (std::vector < std::string> ::iterator is = deferred_removes.begin(); is != deferred_removes.end(); ++is) 
    pollables.erase(*is);
  deferred_removes.clear();
};
    
void 
Pollable::regenFDs() {
  if (regen_pollfds) {
    regen_pollfds = false;
    allpollfds.clear();
    for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); /**/) {
      if (Pollable * ptr = is->second.get()) {
        if (!ptr)
          continue;
        int where = allpollfds.size();
        int numFDs = ptr->getNumPollFDs();
        if (numFDs > 0) {
          ptr->indexInPollFD = where;
          allpollfds.resize(where + numFDs);
          ptr->getPollFDs(& allpollfds[where]);
        } else {
          ptr->indexInPollFD = -1;
        }
        ++is;
      } else {
        PollableSet::iterator to_delete = is;
        ++is;
        pollables.erase(to_delete->first);
      }
    }
  }
}

bool
Pollable::queueOutput(const char *p, uint32_t len, double timestamp) {
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
    int num_bytes = write(pollfd.fd, (char *) aone.first, toWrite);
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

void 
Pollable::asyncMsg(std::string msg) {
  // send an asynchronous message to the control TCP connection (the first tcp connection)
  Pollable *con = lookupByName(controlSocketLabel);
  if (con)
    con->queueOutput(msg);
}

void
Pollable::setControlSocket(std::string label) {
  controlSocketLabel = label;
};

void
Pollable::controlSocketClosed() {
  controlSocketLabel = "";
};

bool 
Pollable::haveControlSocket() {
  return controlSocketLabel.length() > 0;
}


// static initializers
std::vector < struct pollfd > Pollable::allpollfds(5);
PollableSet Pollable::pollables;
std::vector < std::string > Pollable::deferred_removes;
bool Pollable::regen_pollfds = true;
bool Pollable::have_deferrals = false;
bool Pollable::doing_poll = false;
string Pollable::controlSocketLabel = "";

