#ifndef WAVFILEWRITER_HPP
#define WAVFILEWRITER_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"
#include "WavFileHeader.hpp"

class WavFileWriter;
  
class WavFileWriter : public Pollable {

protected:
  string portLabel; // label of port device is attached to
  static const unsigned OUTPUT_BUFFER_SIZE = 16777216; // 16 M output buffer
  static const unsigned MIN_WRITE_SIZE = 65536; // don't call write() with less than this number of bytes, unless file remainder is smaller
  //  boost::circular_buffer < char > outputBuffer;  // output data waiting to be written to file
  string pathTemplate; // template of full path to output file, with %s replaced by date/time of first sample
  int32_t framesToWrite; // number of frames to write to file
  int32_t bytesToWrite;  // number of bytes to write to file
  int32_t byteCountdown; // number of bytes remaining to write
  double lastFrameTimestamp;
  double currFileTimestamp; // timestamp of first sample of current file
  double prevFileTimestamp; // timestamp of first sample of previously written file, for calculating mic digitizer clock bias
  double prevSecondsWritten; // number of seconds written to previous file at nominal rate
  WavFileHeader hdr; // buffer to store header
  bool headerWritten; // has a header been written to the current output file?
  bool timestampCaptured; // has the timestamp for the filename been captured?  If so, don't allow the start of
  // the buffer to be overwritten - we want to guarantee the timestamp is correct for the first frame, and for
  // a buffer's worth of data

  char filename[1024]; // most recently opened file

  uint32_t totalFilesWritten;    // for all completed files
  uint64_t totalSecondsWritten;  // for all completed files

  void openOutputFile(double firstTimestamp);
  void doneOutputFile(int err = 0);

  static void ensureDirs(WavFileWriter *wav); // function called in separate thread to ensure directories for to-be-opened file are open
  enum {DIR_STATE_NONE, DIR_STATE_WAITING, DIR_STATE_CREATED} ensureDirsState; // state of directory creation
public:

  WavFileWriter (string &portLabel, string &label, char *pathTemplate, uint32_t framesToWrite, int rate, int channels);
  
  int getNumPollFDs();

  int getPollFDs (struct pollfd * pollfds);

  int getOutputFD(){return 0;}; 

  bool queueOutput(const char *p, uint32_t len, double timestamp = 0);
    
  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  void stop(double timeNow);

  int start(double timeNow);

  void resumeWithNewFile(string path);

  string toJSON();

  int rate;

  int channels;
};

#endif // WAVFILEWRITER_HPP
