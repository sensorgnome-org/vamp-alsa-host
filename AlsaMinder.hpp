#ifndef ALSAMINDER_HPP
#define ALSAMINDER_HPP

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>

using namespace std;
#include <alsa/asoundlib.h>

#include "Pollable.hpp"
#include "PluginRunner.hpp"
#include "TCPConnection.hpp"

typedef std::set < TCPConnection * > RawListenerSet;

class AlsaMinder : public Pollable {
public:

  static const int  PERIOD_FRAMES         = 2400;   // 50 ms
  static const int  BUFFER_FRAMES         = 131072; // 128K appears to be max buffer size in frames; this is 2.73 seconds
  static const int  MAX_FCD_QUIET_TIME    = 30;     // 30 second maximum quiet time before we decide an FCD data stream is dry and try restart it

    
  string             alsaDev;          // ALSA path to fcd device (e.g. hw:CARD=V10)
  int                rate;             // sampling rate to use for device
  unsigned int       numChan;          // number of channels to read from device
  string             label;            // prefix for lines of output arising from this device, to distinguish them from other devices' output

protected:

  PluginRunnerSet    plugins;          // set of plugins accepting input from this device
  RawListenerSet     rawListeners;     // connections receiving raw output from this device, if any.
  snd_pcm_t *        pcm;              // handle to open pcm device
  snd_pcm_uframes_t  buffer_frames;    // buffer size given to us by ALSA (we attempt to specify it)
  snd_pcm_uframes_t  period_frames;    // period size given to us by ALSA (we attempt to specify it)
  unsigned short     revents;          // demangled version of revent returned after poll()
  long long          totalFrames;      // total frames seen on this FCD since start of capture
  double             startTimestamp;   // timestamp device was (most recently) started (-1 if never)
  double             stopTimestamp;    // timestamp device was (most recently) stopped or opened (-1 if not opened yet)
  double             lastDataReceived; // time at which data was last received; used to detect random FCD stop (e.g. due to hub device reset)
  bool               shouldBeRunning;  // should this FCD be running?
  bool               stopped;          // is this FCD stopped?  (by which we mean not streaming USB audio)
  int                hasError;         // if non-zero, the most recent error this device got while we polled it? (this would have stopped it)
  int                numFD;            // number of file descriptors required for polling on this device

    
public:

  int open();
  void stop(double timeNow);
  void requestStop(double timeNow);
  int start(double timeNow);
  int requestStart(double timeNow);
  void addPluginRunner(PluginRunner *pr);
  void removePluginRunner(PluginRunner *pr);
  void addRawListener(TCPConnection *conn);
  void removeRawListener(TCPConnection *conn);
  void removeAllRawListeners();

  AlsaMinder(string &alsaDev, int rate, unsigned int numChan, string &label, double now, PollableMinder * minder);

  ~AlsaMinder();

  string about();

  string toJSON();

  virtual int getNumPollFDs ();

  virtual int getPollFDs (struct pollfd *pollfds);

  virtual void handleEvents ( struct pollfd *pollfds, bool timedOut, double timeNow);

protected:
  
  void delete_privates();

};

#endif // ALSAMINDER_HPP
