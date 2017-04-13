#ifndef ALSAMINDER_HPP
#define ALSAMINDER_HPP

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <memory>
#include <cmath>

using namespace std;

#include <alsa/asoundlib.h>
#include "DevMinder.hpp"

class AlsaMinder : public DevMinder {
public:

  static const int  PERIOD_FRAMES         = 4800;   // 40 periods per second for FCD Pro +; 20 periods per second for FCD Pro
  static const int  BUFFER_FRAMES         = 131072; // 128K appears to be max buffer size in frames; this is 0.683 s for FCD Pro+, 1.365 s for FCD Pro

protected:

  int               numFD;            // number of file descriptors required for polling on this
                                      // device
  unsigned short    revents;          // demangled version of revent returned after poll()
  snd_pcm_t *       pcm;              // handle to open pcm device
  snd_pcm_uframes_t buffer_frames;    // buffer size given to us by ALSA (we attempt to specify
                                      // it)
  snd_pcm_uframes_t period_frames;    // period size given to us by ALSA (we attempt to specify
                                      // it)
public:

  virtual int hw_open();

  virtual bool hw_is_open();

  AlsaMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now);

  ~AlsaMinder();

  virtual int hw_getNumPollFDs ();

  virtual int hw_getPollFDs (struct pollfd *pollfds);

  virtual int hw_handleEvents ( struct pollfd *pollfds, bool timedOut);

  virtual int hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp);

protected:

  virtual void delete_privates();
  virtual int hw_do_start();
  virtual int hw_do_stop();
  virtual int hw_do_restart();

};

#endif // ALSAMINDER_HPP
