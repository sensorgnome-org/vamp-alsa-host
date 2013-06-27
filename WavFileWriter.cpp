#include "WavFileWriter.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <boost/filesystem.hpp>
#include <iomanip>

#include <unistd.h>

WavFileWriter::WavFileWriter (string &portLabel, string &label, char *pathTemplate, uint32_t framesToWrite, int rate) :
  Pollable(label),
  portLabel(portLabel),
  pathTemplate(pathTemplate),
  framesToWrite(framesToWrite),
  bytesToWrite(framesToWrite * 2), // FIXME: mono S16_LE hardcoded here
  byteCountdown(framesToWrite * 2),
  currFileTimestamp(-1),
  prevFileTimestamp(-1),
  headerWritten(false),
  timestampCaptured(false),
  totalFilesWritten(0),
  totalSecondsWritten(0),
  rate(rate)
{
  pollfd.fd = -1;
  pollfd.events = 0;
  fillWaveFileHeader(rate, 1, framesToWrite); // FIXME: mono S16_LE hardcoded here
  outputBuffer = boost::circular_buffer < char > (OUTPUT_BUFFER_SIZE);
  filename[0]=0;
};

int WavFileWriter::getNumPollFDs() {
  return pollfd.fd >= 0 ? 1 : 0;
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
  // for the first frame.   FIXME: hardcoded assumption of S16_LE and 1 channel

  lastFrameTimestamp = (len - 2) / (2.0 * rate) + timestamp;

  bool rv = Pollable::queueOutput(p, len);
  
  if (pollfd.fd < 0)
    openOutputFile(lastFrameTimestamp - outputBuffer.size() / (2.0 * rate));    

  return rv;
};

void WavFileWriter::openOutputFile(double first_timestamp) {
  if (pathTemplate == "")
    return;
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

  boost::filesystem::create_directories(boost::filesystem::path(filename).parent_path());

  pollfd.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_NOATIME | O_NONBLOCK , S_IRWXU | S_IRWXG);
  requestPollFDRegen();
  if (pollfd.fd < 0) {
    // FIXME: emit an error message so nodejs code can try with a new path
    doneOutputFile(-pollfd.fd);
    return;
  } else {
    pollfd.events |= POLLOUT;
  }
}

void WavFileWriter::doneOutputFile(int err) {
  if (pollfd.fd >= 0) {
    close(pollfd.fd);
    pollfd.fd = -1;
    ++totalFilesWritten;
    totalSecondsWritten += (bytesToWrite - byteCountdown) / (2.0 * rate); // FIXME: hardwired S16_LE mono format 
    prevFileTimestamp = currFileTimestamp;
  }
  requestPollFDRegen();
  std::ostringstream msg;
  msg << "{\"async\":true,\"event\":\"" << (err ? "rawFileError" : "rawFileDone") << "\",\"devLabel\":\"" << portLabel << "\"";
  if (err)
    msg << ",\"errno\":" << err;
  msg << "}\n";        
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
      off_t blam = lseek(pollfd.fd, 0, SEEK_CUR);
      if (blam != 0) {
        std::cerr << "Whoa!: writing header at " << blam << std::endl;
      }
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
    << "\",\"framesWritten\":" << (uint32_t) ((bytesToWrite - byteCountdown) / 2)
    << ",\"framesToWrite\":" << framesToWrite
    << ",\"secondsWritten\":"  << std::setprecision(16) << ((bytesToWrite - byteCountdown) / (2.0 * rate))   
    << ",\"totalFilesWritten\":" << totalFilesWritten
    << ",\"totalSecondsWritten\":" << totalSecondsWritten
    << ",\"prevFileTimestamp\":" << prevFileTimestamp
    << ",\"currFileTimestamp\":" << currFileTimestamp
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
