#ifndef DEVMINDER_HPP
#define DEVMINDER_HPP

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <vector>

using namespace std;

#include "Pollable.hpp"
#include "PluginRunner.hpp"
#include "WavFileHeader.hpp"

typedef std::map < string, boost::weak_ptr < Pollable > > RawListenerSet;
typedef std::map < string, boost::weak_ptr < PluginRunner > > PluginRunnerSet;

class DevMinder : public Pollable {

public:

  static const int  MAX_CHANNELS          = 2;      // maximum of two channels per device
  static const int  MAX_DEV_QUIET_TIME   = 30;     // 30 second maximum quiet time before we decide an device data stream is dry and try restart it

  string             devName;          // path to device (e.g. hw:CARD=V10 for ALSA, or rtlsdr:/tmp/rtlsdr1:3 for rtl_tcp listening on /tmp/rtlsdr1:3
  int                rate;             // sampling rate to supply plugins with
  unsigned int       hwRate;           // sampling rate of hardware device
  unsigned int       numChan;          // number of channels to read from device
  unsigned int       maxSampleAbs;     // maximum absolute value of sample

protected:

  PluginRunnerSet   plugins;          // set of plugins accepting input from this device
  RawListenerSet    rawListeners;     // listeners receiving raw output from this device, if
                                      // any.
  long long         totalFrames;      // total frames seen on this device since start of capture
  double            startTimestamp;   // timestamp device was (most recently) started (-1 if
                                      // never)
  double            stopTimestamp;    // timestamp device was (most recently) stopped or opened
                                      // (-1 if not opened yet)
  double            lastDataReceived; // time at which data was last received (or at which the
                                      // device was most recently started); used to detect
                                      // random audio stop (e.g. due to hub device reset)
                                      // -1 if never started
  bool              shouldBeRunning;  // should this device be running?
  bool              stopped;          // is this device stopped?  (by which we mean not
                                      // streaming USB audio)
  int               hasError;         // if non-zero, the most recent error this device got
                                      // while we polled it? (this would have stopped it)
  bool              demodFMForRaw;    // if true, any rawListeners receive FM-demodulated
                                      // samples (reducing stereo to mono)
  float             demodFMLastTheta; // value of previous phase angle for FM demodulation (in
                                      // range -pi..pi)
  int16_t           downSampleFactor; // by what factor do we downsample input audio for raw listeners
  int16_t           downSampleCount[MAX_CHANNELS];  // count of how many samples we've accumulated since last down sample
  int32_t           downSampleAccum[MAX_CHANNELS];  // accumulator for downsampling
  bool              downSampleUseAvg; // if true, downsample by averaging; else downsample by subsampling

  std::vector < int16_t > sampleBuf;  // buffer to store latest interleaved samples from device

public:

  static DevMinder * getDevMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now); // factory method
  ~DevMinder();

  int open(); // return 0 on success, non-zero on error
  virtual int hw_open() =0 ; // return 0 on success, non-zero on error

  virtual bool hw_is_open() = 0;

  void addPluginRunner(std::string &label, boost::shared_ptr < PluginRunner > pr);
  void removePluginRunner(std::string &label);
  void addRawListener(string &label, int downSampleFactor, bool writeWavHeader = false, bool downSampleUseAvg = false);
  void removeRawListener(string &label);
  void removeAllRawListeners();

  string about();
  string toJSON();

  virtual int getNumPollFDs ();
  virtual int hw_getNumPollFDs () = 0;

  virtual int getPollFDs (struct pollfd *pollfds); // return 0 on sucess; non-zero on error
  virtual int hw_getPollFDs (struct pollfd *pollfds) = 0; // return 0 on success; non-zero on error

  int getOutputFD(){return 0;}; // this kind of Pollable has no output FDs

  virtual void handleEvents ( struct pollfd *pollfds, bool timedOut, double timeNow);
  virtual int hw_handleEvents ( struct pollfd *pollfds, bool timedOut) = 0; // returns number of frames of data available (possibly 0)

  virtual int hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp) = 0;  // fill buffer buf with frame data (interleaved by channel); returns # of frames copied
  // it is guaranteed that whenever hw_handleEvents returns a positive number N, hw_getFrames will be called with numFrames=N
  // also returns CLOCK_REALTIME for first frame in frameTimestamp
  // negative return value is an error code.

  int start(double timeNow);
  void stop(double timeNow);
  void setDemodFMForRaw(bool demod);

protected:

  DevMinder(const string &devName, int rate, unsigned int numChan, unsigned int maxSampleAbs, const string &label, double now, int buffSize); // buffSize is in frames.

  void delete_privates();

  virtual int hw_do_start() = 0;      // returns 0 on success; non-zero otherwise

  int do_restart(double timeNow);
  virtual int hw_do_restart() = 0;    // returns 0 on success; non-zero otherwise

  virtual int hw_do_stop() = 0;       // returns 0 on success; non-zero otherwise

  virtual bool hw_running(double timeNow) = 0;      // is device running?

};

#endif // DEVMINDER_HPP
