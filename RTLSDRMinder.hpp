#ifndef RTLSDRMINDER_HPP
#define RTLSDRMINDER_HPP

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace std;

#include "DevMinder.hpp"

extern "C" {
  // struct that rtl_tcp sends down the data stream
  // each such struct is followed by (size - sizeof(stream_segment_hdr_t) bytes of I/Q sample data.
typedef struct {
        uint32_t size;   // size of this header plus number of sample bytes before next header
        double ts;       // timestamp of first sample in stream
} stream_segment_hdr_t;
};

class RTLSDRMinder : public DevMinder {

protected:

  int                    numFD;       // number of file descriptors required for polling on this device
  int                    rtltcp;      // fd for connection to rtl_tcp server via unix domain socket; -1 means not connected
  struct sockaddr_un     rtltcpAddr;  // address for rtl_tcp server
  std::string            socketPath;  // filesystem path to rtl_tcp unix domain socket
  stream_segment_hdr_t   header;      // most recently encountered header in stream
  bool                   headerValid; // is content of latestHeader valid?
  unsigned int           segi;        // how many bytes from this segment (header + data) have been processed, including those from the header
  unsigned int           bytesAvail;  // bytes available in recv buffer, from latest ioctl()

public:

  const static int RTLSDR_FRAMES = 2048;
  const static int SAMPLE_SCALE = 16;  // amount by which to multiply signed 8-bit samples to get signed 16-bit sample; for plugins, this
                                       // only matters if downsampling by averaging (and then, only improves precision a bit);
                                       // simple subsampling isn't affected, as the scale
                                       // (max absolute value) of the samples incorporates this factor.  Raw outputs, such as audio
                                       // listening on the web interface, and recording of .wav files, are affected.

  virtual int hw_open();

  virtual bool hw_is_open();

  RTLSDRMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now);

  ~RTLSDRMinder();

  virtual int hw_getNumPollFDs ();

  virtual int hw_getPollFDs (struct pollfd *pollfds);

  virtual int hw_handleEvents ( struct pollfd *pollfds, bool timedOut);

  virtual int hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp);

protected:

  virtual void hw_delete_privates();
  virtual int hw_do_start();
  virtual int hw_do_stop();
  virtual int hw_do_restart();

  int getHWRateForRate(int rate); // get minimum sampling rate that is an integer multiple of desired rate; this is the hardware
  // sampling rate that nodejs would have set for this rtlsdr device
  // sets fields hwRate and downsamplefactor correspondingly; returns 0 on sucess, non-zero on error.

};

#endif // RTLSDRMINDER_HPP
