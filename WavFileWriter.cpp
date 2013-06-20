#include "WavFileWriter.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

WavFileWriter::WavFileWriter (string &connLabel, string &label, char *pathTemplate, uint32_t framesToWrite, int rate) :
  Pollable(label),
  connLabel(connLabel),
  pathTemplate(pathTemplate),
  framesToWrite(framesToWrite),
  bytesToWrite(framesToWrite * 2), // FIXME: mono S16_LE hardcoded here
  byteCountdown(framesToWrite * 2),
  headerWritten(false),
  timestampCaptured(false),
  rate(rate)
{
  pollfd.fd = -1;
  pollfd.events = 0;
  fillWaveFileHeader(rate, 1, framesToWrite); // FIXME: mono S16_LE hardcoded here
  outputBuffer = boost::circular_buffer < char > (OUTPUT_BUFFER_SIZE);
};

int WavFileWriter::getNumPollFDs() {
  return pollfd.fd >= 0 ? 1 : 0;
};
  
int WavFileWriter::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

bool WavFileWriter::queueOutput(const char *p, uint32_t len, void * meta) {
  // if we've already opened a file, drop samples that would overflow the
  // buffer
  if (timestampCaptured) {
    len = std::min((int) outputBuffer.reserve(), (int) len);
  }

  if (len == 0)
    return false;

  // if non-null, meta points to a double, which is the timestamp for
  // the first frame; we save the timestamp for the last frame, by
  // adding the rate.  FIXME: hardcoded assumption of S16_LE and 1 channel

  lastFrameTimestamp = (len - 1) / (2.0 * rate) + * (double *) meta;

  bool rv = Pollable::queueOutput(p, len);
  
  if (pollfd.fd < 0)
    openOutputFile(lastFrameTimestamp - len / (2.0 * rate));    

  return rv;
};

void WavFileWriter::openOutputFile(double first_timestamp) {
  timestampCaptured = true;

  // format the timestamp into the filename with 0.1 ms precision
  time_t tt = floor(first_timestamp);
  int extra_6_digits = round((first_timestamp - tt) * 1.0e6);
  char ts[32];
  int nn = strftime(ts, 31, "%Y-%m-%dT%H-%M-%S.", gmtime(&tt));
  sprintf(&ts[nn], "%06d", extra_6_digits);
  snprintf(filename, 1023, pathTemplate.c_str(), ts);

  pollfd.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_NOATIME | O_NONBLOCK , S_IRWXU | S_IRWXG);
  if (pollfd.fd < 0) {
    // FIXME: emit an error message so nodejs code can try with a new path
    doneOutputFile();
    return;
  } else {
    pollfd.events |= POLLOUT;
    requestPollFDRegen();
  }
}

void WavFileWriter::doneOutputFile() {
  if (pollfd.fd >= 0) {
    close(pollfd.fd);
    pollfd.fd = -1;
  }
  requestPollFDRegen();
  timestampCaptured = false;
  headerWritten = false;
  byteCountdown = bytesToWrite;
};
  
void WavFileWriter::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {

  if (pollfds->revents & (POLLERR | POLLHUP | POLLNVAL)) {
    doneOutputFile();
    return;
  }

  if (pollfds->revents & (POLLOUT)) {
    // handle writeable:

    // if a header hasn't been written, write it
    if (! headerWritten) {
      int num_bytes = write(pollfd.fd, (char *) & hdr, sizeof(hdr));
      headerWritten = true;
      if (num_bytes != sizeof(hdr)) {
        // we should deal gracefully with this, but is it ever going
        // to gag on 44 bytes?  Maybe, if the disk is full.
        doneOutputFile();
      }
      return;
    }
    // if there's raw output to send, send as much as we might still need

    int len = outputBuffer.size();
    int nb = writeSomeOutput(std::min(byteCountdown, len));
    byteCountdown -= nb;
    if (nb < 0 || byteCountdown == 0)
      doneOutputFile();
  }
};

void WavFileWriter::stop(double timeNow) {
  /* do nothing */
};

int WavFileWriter::start(double timeNow) {
  /* do nothing */
  return 0;
};

string WavFileWriter::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"type\":\"WavFileWriter\""
    << ",\"fileDescriptor\":" << pollfd.fd
    << ",\"fileName\":\"" << (char *) filename
    << "\",\"framesWritten\":" << (uint32_t) ((bytesToWrite - byteCountdown) / 2)
    << ",\"secondsWritten\":" << ((bytesToWrite - byteCountdown) / (2.0 * rate))   
    << "}";
  return s.str();
};


void WavFileWriter::fillWaveFileHeader (int rate, int numChan, uint32_t frames)
{
  uint32_t bytes = numChan * 2 * frames; // NB: hardcoded S16_LE sample format

  memcpy(hdr.RIFFlabel, "RIFF", 4);
  hdr.remFileSize = bytes + 36;
  memcpy(hdr.WAVElabel, "WAVE", 4);
  memcpy(hdr.FMTlabel, "fmt ", 4);
  hdr.remFmtSize = 16;
  hdr.fmtCode = 1; // PCM
  hdr.numChan = numChan;
  hdr.sampleRate = rate;
  hdr.byteRate = rate * numChan * 2;
  hdr.frameSize = numChan * 2;
  hdr.sampleSize = 16;
  memcpy(hdr.DATAlabel, "data", 4);
  hdr.remDataSize = bytes;
};
