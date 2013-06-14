#include "OutputListener.hpp"

OutputListener::OutputListener(int fd, uint32_t numFrames, void (*setOutputWaiting)(bool), void (*doneCallback)(int32_t), void(*errorCallback)(int32_t)) :
  paused(fd >= 0),
  fd(fd),
  numFrames(numFrames),
  setOutputWaiting(setOutputWaiting),
  doneCallback(doneCallback),
  errorCallback(errorCallback)
  outputBuffer(MAX_OUTPUT_BUFFER_SIZE)
{
};

bool
OutputListener::queueOutput(const char *ptr, uint32_t len, double lastTimestamp=0) {
  if ((unsigned) len > outputBuffer.reserve())
    return false;

  outputBuffer.insert(outputBuffer.end(), ptr, ptr + len);
  if (setOutputWaiting)
    setOutputWaiting(true);

  return true;

};

bool
OutputListener::queueOutput(std::string str) {
  return queueOutput((const char *) str.data(), (uint32_t) str.length());
};

void
OutputListener::setFD(int fd) {
  this->fd = fd;
  if (fd >= 0) {
    paused = false;
    byteCountdown = numFrames * 2; // NB: single-channel S16_LE hardcoded here
  }
};

int
OutputListener::getFD() {
  return fd;
};


void
OutputListener::FDReadyForOutput() {

  if (paused)
    return;

  int len = outputBuffer.size();
  if (len > 0) {
    // write only from the first array; a subsequent call to this handler can write data
    // which is now in the second array but which will eventually be in the first array.
    boost::circular_buffer < char > ::array_range aone = outputBuffer.array_one();
    int to_write = std::min(aone.second, byteCountdown);
    int num_bytes = write(fd, (char *) aone.first, aone.second);
    if (num_bytes < 0) {
      // error writing, call the error callback
      paused = true;
      (*errorCallback)(1);
      return;
    }
    if (num_bytes == len) {
      outputBuffer.clear();
    } else if (num_bytes >= 0) {
      outputBuffer.erase_begin(num_bytes);
    }
    byteCountdown -= num_bytes;
    if (byteCountdown == 0) {
      paused = true;
      (*doneCallback)(1);
    }
  } else {
    (*setOutputWaiting)(false);
  }
}
