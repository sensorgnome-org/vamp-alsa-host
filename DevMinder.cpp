#include "DevMinder.hpp"

// declarations of subclasses for factory method

#include "AlsaMinder.hpp"
#include "RTLSDRMinder.hpp"

void DevMinder::delete_privates() {
  if (Pollable::terminating)
    return;
  for (PluginRunnerSet::iterator ip = plugins.begin(); ip != plugins.end(); /**/) {
    Pollable::remove(ip->first);
    PluginRunnerSet::iterator del = ip++;
    plugins.erase(del);
  }
};

int DevMinder::open() {
  int rv = hw_open();
  downSampleFactor = hwRate / rate;
  return rv;
};

void DevMinder::stop(double timeNow) {
  shouldBeRunning = false;
  Pollable::requestPollFDRegen();
  hw_do_stop();
  stopTimestamp = timeNow;
  stopped = true;
};

int DevMinder::do_restart(double timeNow) {
  hasError = 0;
  startTimestamp = timeNow;
  return hw_do_restart();
}

int DevMinder::start(double timeNow) {
  shouldBeRunning = true;
  if (hw_running(timeNow))
    return 0;
  if (!hw_is_open() && hw_open())
    return 1;
  Pollable::requestPollFDRegen();
  int rv = hw_do_start();
  if (! rv) {
    stopped = false;
    // set timestamps to:
    // - prevent warning about resuming after long pause
    // - allow us to notice no data has been received for too long after startup
    lastDataReceived = startTimestamp = timeNow;
  }
  return rv;
};

void DevMinder::addPluginRunner(std::string &label, boost::shared_ptr < PluginRunner > pr) {
  plugins[label] = pr;
};

void DevMinder::removePluginRunner(std::string &label) {
  // remove plugin runner
  plugins.erase(label);
};

void DevMinder::addRawListener(string &label, int downSampleFactor, bool writeWavHeader, bool downSampleUseAvg) {

  boost::shared_ptr < Pollable > sptr;
  rawListeners[label] = sptr = Pollable::lookupByNameShared(label);
  if (rawListeners.size() == 1) {
    this->downSampleFactor = downSampleFactor;
    this->downSampleUseAvg = downSampleUseAvg;
    for (int i=0; i < MAX_CHANNELS; ++i) {
      downSampleAccum[i] = 0;
      downSampleCount[i] = downSampleFactor;
    }
  }
  if (writeWavHeader) {
    Pollable *ptr = sptr.get();
    if (ptr) {
      // default max possible frames in .WAV header
      // FIXME: hardcoded S16_LE format
      WavFileHeader hdr(hwRate / downSampleFactor, numChan, 0x7ffffffe / 2);
      ptr->queueOutput(hdr.address(), hdr.size());
    }
  }
};

void DevMinder::removeRawListener(string &label) {
  rawListeners.erase(label);
};

void DevMinder::removeAllRawListeners() {
  rawListeners.clear();
};

DevMinder::DevMinder(const string &devName, int rate, unsigned int numChan, unsigned int maxSampleAbs, const string &label, double now, int buffSize):
  Pollable(label),
  devName(devName),
  rate(rate),
  numChan(numChan),
  maxSampleAbs(maxSampleAbs),
  totalFrames(0),
  startTimestamp(-1.0),
  stopTimestamp(now),
  lastDataReceived(-1.0),
  shouldBeRunning(false),
  stopped(true),
  hasError(0),
  demodFMForRaw(false),
  demodFMLastTheta(0),
  sampleBuf(buffSize * numChan)
{
};


DevMinder * DevMinder::getDevMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now) {

  DevMinder * dev;
  if (devName.substr( 0, 7 ) == "rtlsdr:") {
    dev = new RTLSDRMinder(devName, rate, numChan, label, now);
  } else {
    dev = new AlsaMinder(devName, rate, numChan, label, now);
  }
  if (dev->open()) {
    // there was an error, so throw an exception
    dev->delete_privates();
    throw std::runtime_error("Could not open source device or could not set required parameters");
  }
  return dev;
};

DevMinder::~DevMinder() {
  delete_privates();
};

string DevMinder::about() {
  return "Device '" + label + "' = " + devName;
};

string DevMinder::toJSON() {
  ostringstream s;
  s << "{"
    << "\"type\":\"DevMinder\","
    << "\"device\":\"" << devName << "\","
    << "\"rate\":" << rate << ","
    << "\"hwRate\":" << hwRate << ","
    << "\"numChan\":" << numChan << ","
    << setprecision(14)
    << "\"startTimestamp\":" << startTimestamp << ","
    << "\"stopTimestamp\":" << stopTimestamp << ","
    << "\"running\":" << (stopped ? "false" : "true") << ","
    << "\"hasError\":" << hasError << ","
    << "\"totalFrames\":" << totalFrames << ","
    << "\"numRawListeners\":" << rawListeners.size()
    << "}";
  return s.str();
}

int DevMinder::getNumPollFDs () {
  return hw_getNumPollFDs();
};

int DevMinder::getPollFDs (struct pollfd *pollfds) {
  // append pollfd(s) for this object to the specified vector
  if ( hw_getPollFDs(pollfds) ) {
    std::ostringstream msg;
    msg << "\"event\":\"devProblem\",\"error\":\"snd_pcm_poll_descriptors returned error.\",\"devLabel\":\"" << label << "\"";
    Pollable::asyncMsg(msg.str());
    return 1;
  }
  return 0;
}

void DevMinder::handleEvents ( struct pollfd *pollfds, bool timedOut, double timeNow) {
  int avail = hw_handleEvents(pollfds, timedOut);
  if (avail < 0) {
    std::ostringstream msg;
    msg << "\"event\":\"devProblem\",\"error\":\" device returned with error " << (- avail) << "\",\"devLabel\":\"" << label << "\"";
    Pollable::asyncMsg(msg.str());
    hw_do_restart();
    return;
  }

  if (avail > 0)
    lastDataReceived = timeNow;

  if (avail * numChan > sampleBuf.capacity()) {
    sampleBuf.resize(avail * numChan);
  }

  double frameTimestamp;

  avail = hw_getFrames (& sampleBuf[0], avail, frameTimestamp);

  totalFrames += avail;

  if (avail > 0) {

    // FIXME: assumes interleaved channels
    // now downsample sampleBuf, using the running accumulator.
    // We downsample in-place, keeping track of the destination
    // index in downSampleAvail;

    int downSampleAvail = avail;

    if (downSampleFactor > 1) {
      for (unsigned j = 0; j < numChan; ++j) {
        downSampleAvail = 0; // works the same for all channels
        if (downSampleUseAvg) {
          int16_t * rs = & sampleBuf[j];
          int16_t * ds = rs;
          for (int i=0; i < avail; ++i) {
            downSampleAccum[j] += *rs;
            rs += numChan;
            if (! --downSampleCount[j]) {
              downSampleCount[j] = downSampleFactor;
              // simple dithering: round to nearest int, but retain remainder in downSampleAccum
              int16_t downSample = (downSampleAccum[j] + downSampleFactor / 2) / downSampleFactor;
              *ds = downSample;
              downSampleAccum[j] -= downSample * downSampleFactor;
              ds += numChan;
              ++ downSampleAvail;
            }
          }
        } else {
          int16_t * rs = & sampleBuf[j];
          int16_t * ds = rs;
          for (int i=0; i < avail; ++i) {
            if (! --downSampleCount[j]) {
              downSampleCount[j] = downSampleFactor;
              *ds = *rs;
              ds += numChan;
              ++ downSampleAvail;
            }
            rs += numChan;
          }
        }
      }
    }
    // if requested, do FM demodulation of the downsamples,
    if (numChan == 2 && demodFMForRaw) {
      // do in-place FM demodulation with simple but expensive arctan!
      // only first avail slots in sampleBuf will end up valid
      float dthetaScale = hwRate / (2 * M_PI) / 75000.0 * 32767.0;
      for (int i=0; i < downSampleAvail; ++i) {
        // get phase angle in -pi..pi
        float theta = atan2f(sampleBuf[2*i], sampleBuf[2*i+1]);
        float dtheta = theta - demodFMLastTheta;
        demodFMLastTheta = theta;
        if (dtheta > M_PI) {
          dtheta -= 2 * M_PI;
        } else if (dtheta < -M_PI) {
          dtheta += 2 * M_PI;
        }
        sampleBuf[i] = roundf(dthetaScale * dtheta);
      }
    }


    // there are now downSampleAvail samples, stored in sampleBuf[0..downSampleAvail * numChan - 1]

    for (RawListenerSet::iterator ir = rawListeners.begin(); ir != rawListeners.end(); /**/) {

      if (Pollable * ptr = (ir->second).lock().get()) {
        ptr->queueOutput((char *) & sampleBuf[0], downSampleAvail * 2 * numChan, frameTimestamp ); // NB: hardcoded S16_LE sample size
        ++ir;
      } else {
        RawListenerSet::iterator to_delete = ir++;
        rawListeners.erase(to_delete);
      }
    }
    /*
      copy from sampleBuf to each attached plugin's buffer,
      converting from S16_LE to float, and calling the plugin if its
      buffer has reached blocksize
    */

    for (PluginRunnerSet::iterator ip = plugins.begin(); ip != plugins.end(); /**/) {
      if (boost::shared_ptr < PluginRunner > ptr = (ip->second).lock()) {
        ptr->handleData(downSampleAvail, & sampleBuf[0], & sampleBuf[1], 2, frameTimestamp);
        ++ip;
      } else {
        PluginRunnerSet::iterator to_delete = ip++;
        plugins.erase(to_delete);
      }
    }
  } else if (shouldBeRunning && lastDataReceived >= 0 && timeNow - lastDataReceived > MAX_DEV_QUIET_TIME
             && ! (timeNow > 1000000000 && lastDataReceived < 1000000000)) {
    // this device appears to have stopped delivering audio; try restart it
    std::ostringstream msg;
    msg << "\"event\":\"devStalled\",\"error\":\"no data received for " << (timeNow - lastDataReceived) << " secs;\",\"devLabel\":\"" << label << "\"";
    Pollable::asyncMsg(msg.str());
    lastDataReceived = timeNow; // wait before next restart
    stop(timeNow);
    Pollable::requestPollFDRegen();
  }
};


void
DevMinder::setDemodFMForRaw(bool demod) {
  demodFMForRaw = demod;
};
