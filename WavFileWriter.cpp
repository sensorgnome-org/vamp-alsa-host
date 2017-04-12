#include "WavFileWriter.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/thread/thread.hpp>
#include <iomanip>

#include <unistd.h>

WavFileWriter::WavFileWriter (string &portLabel, string &label, char *pathTemplate, uint32_t framesToWrite, int rate, int channels) :
  Pollable(label),
  portLabel(portLabel),
  pathTemplate(pathTemplate),
  framesToWrite(framesToWrite),
  bytesToWrite(framesToWrite * 2 * channels), // FIXME: mono S16_LE hardcoded here
  byteCountdown(framesToWrite * 2 * channels),
  currFileTimestamp(-1),
  prevFileTimestamp(-1),
  hdr(rate, channels, framesToWrite),
  headerWritten(false),
  timestampCaptured(false),
  totalFilesWritten(0),
  totalSecondsWritten(0),
  ensureDirsState(DIR_STATE_NONE),
  rate(rate),
  channels(channels)
{
  pollfd.fd = -1;
  pollfd.events = 0;
  outputBuffer = boost::circular_buffer < char > (OUTPUT_BUFFER_SIZE);
  filename[0]=0;
};

int WavFileWriter::getNumPollFDs() {
  return (pollfd.fd >= 0) ? 1 : 0;
};

int WavFileWriter::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

bool WavFileWriter::queueOutput(const char *p, uint32_t len, double timestamp) {
  // if we've already opened a file, drop samples that would overflow the
  // buffer
  if (timestampCaptured) {
    len = std::min((uint32_t) outputBuffer.reserve(), len);
  }

  if (len == 0)
    return false;

  // get the timestamp for the last frame we're adding, from the timestamp
  // for the first frame.   FIXME: hardcoded assumption of S16_LE

  lastFrameTimestamp = (len - 2 * channels) / (2.0 * channels * rate) + timestamp;

  bool rv = Pollable::queueOutput(p, len);

  if (pollfd.fd < 0)
    openOutputFile(lastFrameTimestamp - outputBuffer.size() / (2.0 * channels * rate));

  // only set this fd up for output polling if there's MIN_WRITE_SIZE data
  // otherwise, we're calling write() much too often

  if ((int) outputBuffer.size() >= MIN_WRITE_SIZE || byteCountdown < MIN_WRITE_SIZE)
    pollfd.events |= POLLOUT;
  else
    pollfd.events &= ~POLLOUT;

  return rv;
};

void WavFileWriter::openOutputFile(double first_timestamp) {
  if (pathTemplate == "")
    return;

  switch (ensureDirsState) {
  case DIR_STATE_WAITING:
    // do nothing
    break;

  case DIR_STATE_NONE:
    {
      prevFileTimestamp = currFileTimestamp;
      currFileTimestamp = first_timestamp;
      timestampCaptured = true;
      // format the timestamp into the filename with fractional second precision
      time_t tt = floor(first_timestamp);
      strftime(filename, 1023, pathTemplate.c_str(), gmtime(&tt));
      char *frac_sec = strstr(filename, "%Q");
      if (frac_sec) {
        int n = 1;
        while (frac_sec[n] == 'Q')
          ++n;
        if (n > 10)
          n = 10;
        static char digfmt[] = "%.Xf"; // NB: 'X' replaced by digit count below
        static char digout[12];
        digfmt[2] = '0' + (n-1);
        snprintf(digout, n+3, digfmt, first_timestamp - tt);
        memcpy(frac_sec, digout+1, n); // NB: skip leading zero
      }

      ensureDirsState = DIR_STATE_WAITING;
      boost::thread (this->ensureDirs, this);
    }
    break;

  case DIR_STATE_CREATED:
    pollfd.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_NOATIME | O_NONBLOCK , S_IRWXU | S_IRWXG);
    requestPollFDRegen();
    if (pollfd.fd < 0) {
      // FIXME: emit an error message so nodejs code can try with a new path
      doneOutputFile(-pollfd.fd);
      return;
    } else {
      pollfd.events |= POLLOUT;
    }
    ensureDirsState = DIR_STATE_NONE;
    break;
  }
}

void WavFileWriter::ensureDirs(WavFileWriter *wav) {
  // yield immediately upon creation so we hopefully don't run until main thread is polling
  boost::this_thread::yield();
  boost::filesystem::create_directories(boost::filesystem::path(wav->filename).parent_path());
  wav->ensureDirsState = DIR_STATE_CREATED;
};

void WavFileWriter::doneOutputFile(int err) {
  if (pollfd.fd >= 0) {
    close(pollfd.fd);
    pollfd.fd = -1;
    ++totalFilesWritten;
    prevSecondsWritten = (bytesToWrite - byteCountdown) / (2.0 * channels * rate); // FIXME: hardwired S16_LE format
    totalSecondsWritten += prevSecondsWritten;
  }
  requestPollFDRegen();

  std::ostringstream msg;
  msg << "\"async\":true,\"event\":\"" << (err ? "rawFileError" : "rawFileDone") << "\",\"devLabel\":\"" << portLabel << "\"";
  if (err)
    msg << ",\"errno\":" << err;
  Pollable::asyncMsg(msg.str());
  if (err)
    Pollable::remove(label);
  else
    pathTemplate = "";
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
      size_t num_bytes = write(pollfd.fd, hdr.address(), hdr.size());
      headerWritten = true;
      if (num_bytes != hdr.size()) {
        // we should deal gracefully with this, but is it ever going
        // to gag on 44 bytes?  Maybe, if the disk is full.
        doneOutputFile();
      }
      return;
    }
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

void
WavFileWriter::resumeWithNewFile(string path) {
  pollfd.fd = -1;
  pathTemplate = path;
  headerWritten = false;
  timestampCaptured = false;
  byteCountdown = bytesToWrite;
};


string WavFileWriter::toJSON() {
  ostringstream s;
  s << "{"
    << "\"type\":\"WavFileWriter\""
    << ",\"port\":\"" << portLabel
    << "\",\"fileDescriptor\":" << pollfd.fd
    << ",\"fileName\":\"" << (char *) filename
    << "\",\"framesWritten\":" << (uint32_t) ((bytesToWrite - byteCountdown) / (2 * channels))
    << ",\"framesToWrite\":" << framesToWrite
    << ",\"secondsWritten\":"  << std::setprecision(16) << ((bytesToWrite - byteCountdown) / (2.0 * rate * channels))
    << ",\"secondsToWrite\":" << framesToWrite / (double) rate
    << ",\"totalFilesWritten\":" << totalFilesWritten
    << ",\"totalSecondsWritten\":" << totalSecondsWritten
    << ",\"prevFileTimestamp\":" << prevFileTimestamp
    << ",\"currFileTimestamp\":" << currFileTimestamp
    << ",\"prevSecondsWritten\":" << prevSecondsWritten
    << ",\"rate\":" << rate
    << "}";
  return s.str();
};
