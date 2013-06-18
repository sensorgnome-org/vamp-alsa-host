#ifndef OUTPUTLISTENER_HPP
#define OUTPUTLISTENER_HPP

/* an object that receives data and sends it to a file descriptor */

#include <string>
#include <stdexcept>

class OutputListener {
public:

  static const unsigned MAX_OUTPUT_BUFFER_SIZE = 2097152; // maximum size of raw frame buffer in bytes; ~ 11 seconds at 48k in 16-bit stereo

  OutputListener(int fd, uint32_t numFrames, void (*setOutputWaiting)(bool), void (*doneCallback)(int32_t), void(*errorCallback)(int32_t));

  bool queueOutput(const char *ptr, uint32_t len, double lastTimestamp=0); // return true if output was queued

  bool queueOutput(std::string str);

  void setFD(int fd);

  int getFD();

  void readyForOutput(); // the output file descriptor has come up as ready-to-write in a call to poll(), so send output to it

protected:
  bool paused; // if true, data can be queued but are not output
  int fd; // only used if non-negative
  boost::circular_buffer < char > outputBuffer;  // output data waiting to be written to fd
  uint32_t numFrames; // number of frames to write before calling doneCallback
  uint32_t byteCountDown; // number of bytes left to write before calling doneCallback
  double lastTimestamp; // timestamp of last frame appended to outputBuffer

  void (*setOutputWaiting)(bool);
  void (*doneCallback)(int32_t);
  void (*errorCallback)(int32_t);
};

#endif /* OUTPUTLISTENER_HPP */
