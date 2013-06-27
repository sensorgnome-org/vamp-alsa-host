#ifndef WAVFILEWRITER_HPP
#define WAVFILEWRITER_HPP

#include <string>
#include <sstream>
#include <boost/circular_buffer.hpp>

using std::string;
using std::istringstream;

#include "Pollable.hpp"

struct WavFileHeader {
  char RIFFlabel[4];
  uint32_t remFileSize;
  char WAVElabel[4];
  char FMTlabel[4];
  uint32_t remFmtSize;
  uint16_t fmtCode;
  uint16_t numChan;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t frameSize;
  uint16_t sampleSize;
  char DATAlabel[4];
  uint32_t remDataSize;
} __attribute__((packed));
  
class WavFileWriter : public Pollable {

protected:
  string portLabel; // label of port device is attached to
  static const unsigned OUTPUT_BUFFER_SIZE = 16777216; // 16 M output buffer

  //  boost::circular_buffer < char > outputBuffer;  // output data waiting to be written to file
  string pathTemplate; // template of full path to output file, with %s replaced by date/time of first sample
  int32_t framesToWrite; // number of frames to write to file
  int32_t bytesToWrite;  // number of bytes to write to file
  int32_t byteCountdown; // number of bytes remaining to write
  double lastFrameTimestamp;
  double currFileTimestamp; // timestamp of first sample of current file
  double prevFileTimestamp; // timestamp of first sample of previously written file, for calculating mic digitizer clock bias
  WavFileHeader hdr; // buffer to store header
  bool headerWritten; // has a header been written to the current output file?
  bool timestampCaptured; // has the timestamp for the filename been captured?  If so, don't allow the start of
  // the buffer to be overwritten - we want to guarantee the timestamp is correct for the first frame, and for
  // a buffer's worth of data

  char filename[1024]; // most recently opened file

  uint32_t totalFilesWritten;    // for all completed files
  uint64_t totalSecondsWritten;  // for all completed files

  void fillWaveFileHeader (int rate, int numChan, uint32_t frames); // fill the .WAV file header 

  void openOutputFile(double firstTimestamp);
  void doneOutputFile(int err = 0);

public:

  WavFileWriter (string &portLabel, string &label, char *pathTemplate, uint32_t framesToWrite, int rate);
  
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
};

#endif // WAVFILEWRITER_HPP
