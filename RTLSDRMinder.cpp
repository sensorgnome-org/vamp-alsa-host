#include "RTLSDRMinder.hpp"
#include <sys/ioctl.h>

#define UNIX_PATH_MAX 108

void RTLSDRMinder::hw_delete_privates() {
  if (rtltcp >= 0) {
    close(rtltcp);
    rtltcp = -1;
  }
};

int RTLSDRMinder::hw_open() {
  rtltcp = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (rtltcp < 0) {
    std::cerr << "unable to open socket fd for rtlsdr\n";
    return 1;
  }
  memset(& rtltcpAddr, 0, sizeof(struct sockaddr_un));
  rtltcpAddr.sun_family = AF_UNIX;
  snprintf(rtltcpAddr.sun_path, UNIX_PATH_MAX, socketPath.c_str());
  if(connect(rtltcp, (struct sockaddr *) &rtltcpAddr, sizeof(struct sockaddr_un))) {
    std::cerr << "unable to connect to socket for rtlsdr\n";
    return 2;
  }
  return 0;
};

bool RTLSDRMinder::hw_is_open() {
  return rtltcp >= 0;
};

int RTLSDRMinder::hw_do_stop() {
  return 0;
};

int RTLSDRMinder::hw_do_start() {
  if (rtltcp < 0 && open())
    return 1;
  hasError = 0;
  return 0;
}

int RTLSDRMinder::hw_do_restart() {
  hasError = 0;
  return 0;
};

RTLSDRMinder::RTLSDRMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now):
  DevMinder(devName, rate, numChan, label, now, RTLSDR_FRAMES),
  numFD(1),
  rtltcp(-1)
{
  if (devName.substr(0, 7) != "rtlsdr:")
    throw std::runtime_error("Invalid name for RTLSDR device; must look like 'rtlsdr:PATH'");
  socketPath = devName.substr(7);
};

RTLSDRMinder::~RTLSDRMinder() {
};

int RTLSDRMinder::hw_getNumPollFDs () {
  return numFD;
};

int RTLSDRMinder::hw_getPollFDs (struct pollfd *pollfds) {
  if (rtltcp < 0)
    return 1;

  pollfds->fd = rtltcp;
  pollfds->events = POLLIN | POLLPRI;
  return 0;
}

int RTLSDRMinder::hw_handleEvents ( struct pollfd *pollfds, bool timedOut) {
  if (rtltcp < 0)
    return 0;
  if (pollfds->revents & (POLLIN | POLLPRI)) {
    // return number of frames available
    int avail;
    ioctl(rtltcp, FIONREAD, &avail);
    return avail / 2; // hardcoded: 1 byte per sample, two channels (I/Q)
  }
  return 0;
};

int RTLSDRMinder::hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp) {
  // get realtime clock
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  frameTimestamp = ts.tv_sec + ts.tv_nsec / (double) 1.0e9;
  /*
    copy available samples to buf, then expand from 8 to 16 bits
  */
  int bytes = recv(rtltcp, buf, numFrames * 2, 0);
  // use only an even number of bytes
  bytes = bytes & ~1;

  // expand the samples from 8 to 16 bits
  // working from right to left
  int16_t * ebuf = buf + (bytes - 1);
  char * cbuf = ((char *) buf) + (bytes - 1);
  for ( int i = bytes; i > 0; --i)
    *ebuf-- = (int16_t) *cbuf--;
  return bytes;
};
