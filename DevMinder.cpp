#include "DevMinder.hpp"

void DevMinder::delete_privates() {
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  if (Pollable::terminating)
    return;
  for (PluginRunnerSet::iterator ip = plugins.begin(); ip != plugins.end(); /**/) {
    Pollable::remove(ip->first);
    PluginRunnerSet::iterator del = ip++;
    plugins.erase(del);
  }
};

int DevMinder::open() {
  // open the audio device and set our default audio parameters
  // return 0 on success, 1 on error;

  snd_pcm_hw_params_t *params;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_access_mask_t *mask;
  snd_pcm_uframes_t boundary;

  snd_pcm_hw_params_alloca( & params);
  snd_pcm_sw_params_alloca( & swparams);
  snd_pcm_access_mask_alloca( & mask );

  snd_pcm_access_mask_none( mask);
  snd_pcm_access_mask_set( mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);

  int rateDir = 1;

  if ((snd_pcm_open(& pcm, devName.c_str(), SND_PCM_STREAM_CAPTURE, 0))
      || snd_pcm_hw_params_any(pcm, params)
      || snd_pcm_hw_params_set_access_mask(pcm, params, mask)
      || snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE)
      || snd_pcm_hw_params_set_channels(pcm, params, numChan)
      || snd_pcm_hw_params_set_rate_resample(pcm, params, 0)
      || snd_pcm_hw_params_set_rate_last(pcm, params, & hwRate, & rateDir)
      || hwRate % rate != 0 // we only do exact rate decimation
      || snd_pcm_hw_params_set_period_size_near(pcm, params, & period_frames, 0) < 0
      || snd_pcm_hw_params_set_buffer_size_near(pcm, params, & buffer_frames) < 0
      || snd_pcm_hw_params(pcm, params)
      || snd_pcm_sw_params_current(pcm, swparams)
      || snd_pcm_sw_params_set_tstamp_mode(pcm, swparams, SND_PCM_TSTAMP_ENABLE)
#ifdef RPI
      || snd_pcm_sw_params_set_tstamp_type(pcm, swparams, SND_PCM_TSTAMP_TYPE_GETTIMEOFDAY)
#endif
      || snd_pcm_sw_params_set_period_event(pcm, swparams, 1)
      // get the ring buffer boundary, and
      || snd_pcm_sw_params_get_boundary	(swparams, &boundary)
      || snd_pcm_sw_params_set_stop_threshold (pcm, swparams, boundary)
      || snd_pcm_sw_params(pcm, swparams)
      || (numFD = snd_pcm_poll_descriptors_count (pcm)) < 0

      ) {
    return 1;
  }
  return 0;
};

void DevMinder::do_stop(double timeNow) {
  Pollable::requestPollFDRegen();
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  stopTimestamp = timeNow;
  stopped = true;
};

void DevMinder::stop(double timeNow) {
  shouldBeRunning = false;
  do_stop(timeNow);
};

int DevMinder::do_start(double timeNow) {
  if (!pcm && open())
    return 1;
  Pollable::requestPollFDRegen();
  snd_pcm_prepare(pcm);
  if (! (hasError = snd_pcm_start(pcm)) ) {
    stopped = false;
    // set timestamps to:
    // - prevent warning about resuming after long pause
    // - allow us to notice no data has been received for too long after startup
    lastDataReceived = startTimestamp = timeNow;
  }
  return 0;
}

int DevMinder::start(double timeNow) {
  shouldBeRunning = true;
  return do_start(timeNow);
};

void DevMinder::addPluginRunner(std::string &label, shared_ptr < PluginRunner > pr) {
  plugins[label] = pr;
};

void DevMinder::removePluginRunner(std::string &label) {
  // remove plugin runner
  plugins.erase(label);
};

void DevMinder::addRawListener(string &label, int downSampleFactor, bool writeWavHeader, bool downSampleUseAvg = true) {

  shared_ptr < Pollable > sptr;
  rawListeners[label] = sptr = Pollable::lookupByNameShared(label);
  if (rawListeners.size() == 1) {
    this->downSampleFactor = downSampleFactor;
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

DevMinder::DevMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now):
  Pollable(label),
  devName(devName),
  rate(rate),
  numChan(numChan),
  pcm(0),
  buffer_frames(BUFFER_FRAMES),
  period_frames(PERIOD_FRAMES),
  revents(0),
  totalFrames(0),
  startTimestamp(-1.0),
  stopTimestamp(now),
  lastDataReceived(-1.0),
  shouldBeRunning(false),
  stopped(true),
  hasError(0),
  numFD(0),
  demodFMForRaw(false),
  demodFMLastTheta(0)
{
  if (open()) {
    // there was an error, so throw an exception
    delete_privates();
    throw std::runtime_error("Could not open audio device or could not set required parameters");
  }

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
    << "\"totalFrames\":" << totalFrames
    << "}";
  return s.str();
}

int DevMinder::getNumPollFDs () {
  return (pcm && shouldBeRunning) ? numFD : 0;
};

int DevMinder::getPollFDs (struct pollfd *pollfds) {
  // append pollfd(s) for this object to the specified vector
  // ALSA weirdness means there may be more than one fd per audio device
  if (pcm && shouldBeRunning) {
    if (numFD != snd_pcm_poll_descriptors(pcm, pollfds, numFD)) {
      std::ostringstream msg;
      msg << "\"event\":\"devProblem\",\"error\":\"snd_pcm_poll_descriptors returned error.\",\"devLabel\":\"" << label << "\"";
      Pollable::asyncMsg(msg.str());
      return 1;
    }
  }
  return 0;
}

void DevMinder::handleEvents ( struct pollfd *pollfds, bool timedOut, double timeNow) {
  if (!pcm)
    return;
  short unsigned revents;
  if (!timedOut) {
    int rv = snd_pcm_poll_descriptors_revents( pcm, pollfds, numFD, & revents);
    if (rv != 0) {
      throw std::runtime_error(about() + ": snd_pcm_poll_descriptors_revents returned error.\n");
    }
  } else {
    revents = 0;
  }
  if (revents & (POLLIN | POLLPRI)) {
    // copy as much data as possible from mmap ring buffer
    // and inform any pluginRunners that we have data

    snd_pcm_sframes_t avail = snd_pcm_avail_update (pcm);
    if (avail < 0) {
      snd_pcm_recover(pcm, avail, 1);
      snd_pcm_prepare(pcm);
      hasError = 0;
      snd_pcm_start(pcm);
      startTimestamp = timeNow;

    } else if (avail > 0) {
      lastDataReceived = timeNow;

      double frameTimestamp;

      // get most recent period timestamp from ALSA
      snd_htimestamp_t ts;
      snd_pcm_uframes_t av;
      snd_pcm_htimestamp(pcm, &av, &ts);
      frameTimestamp = ts.tv_sec + (double) ts.tv_nsec / 1.0e9 - (double) av / hwRate;

      // begin direct access to ALSA mmap buffers for the device
      const snd_pcm_channel_area_t *areas;
      snd_pcm_uframes_t offset;
      snd_pcm_uframes_t have = (snd_pcm_sframes_t) avail;

      int errcode;
      if ((errcode = snd_pcm_mmap_begin (pcm, & areas, & offset, & have))) {
        std::ostringstream msg;
        msg << "\"event\":\"devProblem\",\"error\":\" snd_pcm_mmap_begin returned with error " << (-errcode) << "\",\"devLabel\":\"" << label << "\"";
        Pollable::asyncMsg(msg.str());
        return;
      }
      avail = have;

      totalFrames += avail;
      int16_t *src0, *src1=0; // avoid compiler warning
      int step;

      /*
        if a raw output listener exists, queue the new data onto it
      */

      if (rawListeners.size() > 0) {
        // FIXME: assumes interleaved channels
        src0 = (int16_t *) (((unsigned char *) areas[0].addr) + areas[0].first / 8);
        step = areas[0].step / 16; // FIXME:  hardcoding S16_LE assumption
        src0 += step * offset;

        int16_t rawSamples[numChan * avail];
        if (numChan == 2 && demodFMForRaw) {
          // do FM demodulation with simple but expensive arctan!
          float dthetaScale = hwRate / (2 * M_PI) / 75000.0 * 32767.0;
          int16_t * samps = (int16_t *) src0;
          for (int i=0; i < avail; ++i) {
            // get phase angle in -pi..pi
            float theta = atan2f(samps[2*i], samps[2*i+1]);
            float dtheta = theta - demodFMLastTheta;
            demodFMLastTheta = theta;
            if (dtheta > M_PI) {
              dtheta -= 2 * M_PI;
            } else if (dtheta < -M_PI) {
              dtheta += 2 * M_PI;
            }
            rawSamples[i] = roundf(dthetaScale * dtheta);
          }
        } else {
          for (unsigned i=0; i < avail * numChan; ++i) {
            rawSamples[i] = *src0;
            //            src0 += step;
            ++src0;
          }
        }

        // now downsample rawSamples, using the running accumulator
        // we downsample in-place, keeping track of the destination
        // index in downSampleAvail;

        int downSampleAvail = avail;

        if (downSampleFactor > 1) {
          for (unsigned j = 0; j < numChan; ++j) {
            int16_t * rs = & rawSamples[j];
            int16_t * ds = rs;
            downSampleAvail = 0; // works the same for all channels
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
          }
        }

        // there are now downSampleAvail samples, stored in rawSamples[0..downSampleAvail * numChan - 1]

        for (RawListenerSet::iterator ir = rawListeners.begin(); ir != rawListeners.end(); /**/) {

          if (Pollable * ptr = (ir->second).lock().get()) {
            ptr->queueOutput((char *) rawSamples, downSampleAvail * 2 * numChan, frameTimestamp ); // NB: hardcoded S16_LE sample size
            ++ir;
          } else {
            RawListenerSet::iterator to_delete = ir++;
            rawListeners.erase(to_delete);
          }
        }
      }
      /*
      copy from ALSA mmap buffers to each attached plugin's buffer,
      converting from S16_LE to float, and calling the plugin if its
      buffer has reached blocksize
      */

      for (PluginRunnerSet::iterator ip = plugins.begin(); ip != plugins.end(); /**/) {
        // we are going out on a limb and assuming the step is the same for both channels

        src0 = (int16_t *) (((unsigned char *) areas[0].addr) + areas[0].first / 8);
        step = areas[0].step / 16; // FIXME:  hardcoding S16_LE assumption
        src0 += step * offset;

        if (numChan == 2) {
          src1 = (int16_t *) (((unsigned char *) areas[1].addr) + areas[1].first / 8);
          src1 += step * offset;
        }

        if (boost::shared_ptr < PluginRunner > ptr = (ip->second).lock()) {
          ptr->handleData(avail, src0, src1, step, frameTimestamp);
          ++ip;
        } else {
          PluginRunnerSet::iterator to_delete = ip++;
          plugins.erase(to_delete);
        }
      }
      /*
      Tell ALSA we're finished using its internal mmap buffer.  We do
      this after calling the plugin, which means we may be hanging on
      to it for a long time, but this way we can process all available
      data with a single pair of calls to snd_pcm_mmap_begin and
      snd_pcm_mmap_commit. We've tried to make the buffer for each
      device much larger than a single period, so that ALSA has plenty
      of room to store new data even while we have this chunk of its
      ring buffer locked up.
      */

      if (0 > snd_pcm_mmap_commit (pcm, offset, avail)) {
        std::ostringstream msg;
        msg << "\"event\":\"devProblem\",\"error\":\" snd_pcm_mmap_commit returned with error " << (-errcode) << "\",\"devLabel\":\"" << label << "\"";
        Pollable::asyncMsg(msg.str());
      }
    }
  } else if (shouldBeRunning && lastDataReceived >= 0 && timeNow - lastDataReceived > MAX_AUDIO_QUIET_TIME) {
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
