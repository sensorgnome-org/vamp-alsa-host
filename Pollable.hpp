#ifndef POLLABLE_HPP
#define POLLABLE_HPP

/* a quasi-interface to allow an object to participate in polling by a VampAlsaHost 
   all Pollable objects created become part of a set indexed by string labels, and
   each one has a set of FDs which can participate in polling.  Participation can
   be enabled and disabled.
*/

#include <string>
#include <stdexcept>
#include <stdint.h>
#include <boost/circular_buffer.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>

//using boost::make_shared;
//using boost::shared_ptr;
//using boost::weak_ptr;
//using boost::static_pointer_cast;

#include "VampAlsaHost.hpp"

class Pollable;
typedef std::map < std::string, boost::shared_ptr<Pollable> > PollableSet;

class Pollable {
public:
  /* class members */

  static const unsigned DEFAULT_OUTPUT_BUFFER_SIZE = 16384; // default size of output buffer; subclasses may request larger 

  static bool terminating; // if true, don't call e.g. map functions from destructors of element maps!
  static PollableSet pollables; // map of Pollables, indexed by label, values are shared pointers
  static void remove(const string label);
  static Pollable * lookupByName (const std::string& label);
  static boost::shared_ptr < Pollable > lookupByNameShared (const std::string& label);
  static void requestPollFDRegen();
  static int poll(int timeout); // do one round of polling; return 0 on okay; errno otherwise
  static void setControlSocket(string label);
  static void controlSocketClosed();
  static bool haveControlSocket();

protected:
  static std::vector <struct pollfd> allpollfds; // in same order as pollables, but some pollables may have 0 or more than 1 FD
  static std::vector < std::string > deferred_removes;
  static bool regen_pollfds;
  static bool have_deferrals;
  static bool doing_poll;
  static void doDeferrals();  
  static void regenFDs();
  static void asyncMsg(std::string msg); // send an asynchronous message to the control TCP connection (the first tcp connection)
  static string controlSocketLabel;

  /* instance members */

public:

  Pollable(const string label);

  virtual ~Pollable();

  string label;
  virtual string toJSON() = 0;
  virtual bool queueOutput(const char * p, uint32_t len, double timestamp = 0.0);
  virtual bool queueOutput(std::string &str, double timestamp = 0) {return queueOutput(str.data(), str.length(), timestamp);};
  int writeSomeOutput(int maxBytes);

  short & eventsOf(int offset = 0); // reference to the events field for a pollfd

  virtual int getNumPollFDs() {return 0;};                      // return number of fds used by this Pollable (negative means error)
  virtual int getPollFDs (struct pollfd * pollfds) {return 0;}; // copy pollfds for this Pollable to the location specified (return non-zero on error)
  // (i.e. this reports pollable fds and the pollfd "events" field for this object)
  virtual int getOutputFD() {return -1;}; // return the output pollfd, if applicable
  virtual void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {}; // handle possible event(s) on the fds for this Pollable
  virtual int start(double timeNow){ return 0;};
  virtual void stop(double timeNow){};

protected:
  int indexInPollFD;  // index of first FD in class pollfds vector (< 0 means not in pollfd vector)
  struct pollfd pollfd;

  boost::circular_buffer < char > outputBuffer;
  bool outputPaused;
};

#endif /* POLLABLE_HPP */
