#include "AlsaMinder.hpp"

void AlsaMinder::delete_privates() {
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  for (PluginRunnerSet::iterator ip = plugins.begin(); ip != plugins.end(); /**/) {
    host->remove(ip->second);
    PluginRunnerSet::iterator del = ip++;
    plugins.erase(del);
  }
};
    
int AlsaMinder::open() {
  // open the audio device and set our default audio parameters
  // return 0 on success, 1 on error;

  snd_pcm_hw_params_t *params;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_access_mask_t *mask;

  snd_pcm_hw_params_alloca( & params);
  snd_pcm_sw_params_alloca( & swparams);
  snd_pcm_access_mask_alloca( & mask );
        
  snd_pcm_access_mask_none( mask);
  snd_pcm_access_mask_set( mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);

  int rateDir = 1;

  if ((snd_pcm_open(& pcm, alsaDev.c_str(), SND_PCM_STREAM_CAPTURE, 0))
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
      || snd_pcm_sw_params_set_period_event(pcm, swparams, 1)
      || snd_pcm_sw_params(pcm, swparams)
      || (numFD = snd_pcm_poll_descriptors_count (pcm)) < 0

      ) {
    return 1;
  } 
  return 0;
};

void AlsaMinder::do_stop(double timeNow) {
  host->requestPollFDRegen();
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  stopTimestamp = timeNow;
  stopped = true;
};

void AlsaMinder::stop(double timeNow) {
  shouldBeRunning = false;
  do_stop(timeNow);
};

int AlsaMinder::do_start(double timeNow) {
  if (!pcm && open())
    return 1;
  host->requestPollFDRegen();
  snd_pcm_prepare(pcm);
  hasError = 0;
  snd_pcm_start(pcm);
  stopped = false;
  // set timestamps to:
  // - prevent warning about resuming after long pause
  // - allow us to notice no data has been received for too long after startup
  lastDataReceived = startTimestamp = timeNow; 
  return 0;
}

int AlsaMinder::start(double timeNow) {
  shouldBeRunning = true;
  return do_start(timeNow);
};

void AlsaMinder::addPluginRunner(std::shared_ptr < PluginRunner > pr) {
  plugins[pr.get()] = pr;
};

void AlsaMinder::removePluginRunner(std::shared_ptr < PluginRunner > pr) {
  // remove plugin runner 
  plugins.erase(pr.get());
};

void AlsaMinder::addRawListener(string connLabel, unsigned long long framesBetweenTimestamps, int downSampleFactor) {
  
  std::shared_ptr < TCPConnection > conn = static_pointer_cast < TCPConnection > (host->lookupByNameShared(connLabel));
  if (conn) {
    rawListeners[connLabel] = {conn, framesBetweenTimestamps, 0};
    this->downSampleFactor = downSampleFactor;
    downSampleCount = downSampleFactor;
    downSampleAccum = 0;
  }
};

void AlsaMinder::removeRawListener(string connLabel) {
  rawListeners.erase(connLabel);
};

void AlsaMinder::removeAllRawListeners() {
  rawListeners.clear();
};

AlsaMinder::AlsaMinder(string &alsaDev, int rate, unsigned int numChan, string &label, double now, VampAlsaHost *minder):
  Pollable(minder, label),
  alsaDev(alsaDev),
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

AlsaMinder::~AlsaMinder() {
  delete_privates();
};

string AlsaMinder::about() {
  return "Device '" + label + "' = " + alsaDev;
};

string AlsaMinder::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"type\":\"AlsaMinder\","
    << "\"device\":\"" << alsaDev << "\","
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

int AlsaMinder::getNumPollFDs () {
  return (pcm && shouldBeRunning) ? numFD : 0;
};

int AlsaMinder::getPollFDs (struct pollfd *pollfds) {
  // append pollfd(s) for this object to the specified vector
  // ALSA weirdness means there may be more than one fd per audio device
  if (pcm && shouldBeRunning) {
    if (numFD != snd_pcm_poll_descriptors(pcm, pollfds, numFD)) {
      cerr << "{\"event\":\"devProblem\",\"error\":\"snd_pcm_poll_descriptors returned error.\",\"devLabel\":\"" << label << "\"}" << endl;
      cerr.flush();
      return 1;
    }
  }
  return 0;
}

void AlsaMinder::handleEvents ( struct pollfd *pollfds, bool timedOut, double timeNow) {
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

  if (revents & POLLERR) {
    hasError = errno;
    stopped = true;
    host->requestPollFDRegen();
    stop(timeNow);
    if (start(timeNow)) {
      cerr << "{\"event\":\"devStalled\",\"devLabel\":\"" << label << "\",\"error\":\"poll return with POLLERR and errno=" << errno << "\"}" << endl;
      cerr.flush();
    }
    return;
  }
  if (revents & (POLLIN | POLLPRI)) {
    // copy as much data as possible from mmap ring buffer
    // and inform any pluginRunners that we have data

    snd_pcm_sframes_t avail = snd_pcm_avail_update (pcm);
    if (avail < 0) {
      cerr << "{\"event\":\"devStalled\",\"error\":\"snd_pcm_avail_update() when POLLIN|POLLPRI was true returned with error " << (-avail) << "\",\"devLabel\":\"" << label << "\"}" << endl;
      cerr.flush();
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
        cerr << "{\"event\":\"devProblem\",\"error\":\" snd_pcm_mmap_begin returned with error " << (-errcode) << "\",\"devLabel\":\"" << label << "\"}" << endl;
        cerr.flush();
        return;
      }
      avail = have;
   
      totalFrames += avail;
      int16_t *src0, *src1=0; // avoid compiler warning
      int step;

      /*
        if a raw output connection exists, queue the new data onto it
      */

      if (rawListeners.size() > 0) {
        src0 = (int16_t *) (((unsigned char *) areas[0].addr) + areas[0].first / 8);
        step = areas[0].step / 16; // FIXME:  hardcoding S16_LE assumption
        src0 += step * offset;

        int16_t rawSamples[avail];
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
          for (int i=0; i < avail; ++i) {
            rawSamples[i] = *src0;
            src0 += step;
          }
        }

        // now downsample rawSamples, using the running accumulator
        // we downsample in-place, keeping track of the destination
        // index in downSampleAvail;

        int downSampleAvail = 0;
        for (int i=0; i < avail; ++i) {
          downSampleAccum += rawSamples[i];
          if (! --downSampleCount) {
            downSampleCount = downSampleFactor;
            // simple dithering: round to nearest int, but retain remainder in downSampleAccum
            int16_t downSample = (downSampleAccum + downSampleFactor / 2) / downSampleFactor;
            rawSamples[downSampleAvail++] = downSample;
            downSampleAccum -= downSample * downSampleFactor;
          }
        }

        // there are now downSampleAvail samples, stored in rawSamples[0..downSampleAvail - 1]

        for (RawListenerSet::iterator ir = rawListeners.begin(); ir != rawListeners.end(); /**/) {

          if (auto ptr = (ir->second.con).lock()) {
            // if we're emitting timestamps between frames, see whether
            // we'll need to do that before the end of this batch
          
            if (ir->second.framesBetweenTimestamps > 0) {
              long long after_timestamp = downSampleAvail - ir->second.frameCountDown;
              ptr->queueOutput((char *) rawSamples, std::min(ir->second.frameCountDown, (unsigned long long) downSampleAvail) * 2);
              if (after_timestamp >= 0) {
                ptr->queueOutput((char *) &frameTimestamp, sizeof(frameTimestamp));
                if (after_timestamp > 0)
                  ptr->queueOutput((char *) rawSamples, after_timestamp * 2);
                ir->second.frameCountDown = ir->second.framesBetweenTimestamps - after_timestamp;
              } else {
                ir->second.frameCountDown -= downSampleAvail;
              }
            } else {
              ptr->queueOutput((char *) rawSamples, downSampleAvail * 2);
            }
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

        if (auto ptr = (ip->second).lock()) {
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
        cerr << "{\"event\":\"devProblem\",\"error\":\" snd_pcm_mmap_commit returned with error " << (-errcode) << "\",\"devLabel\":\"" << label << "\"}" << endl;
        cerr.flush();
      }
    }
  } else if (shouldBeRunning && lastDataReceived >= 0 && timeNow - lastDataReceived > MAX_AUDIO_QUIET_TIME) {
    // this device appears to have stopped delivering audio; try restart it
    cerr << "{\"event\":\"buffer overflow?\",\"error\":\"no data received for " << (timeNow - lastDataReceived) << " secs; restarting\",\"devLabel\":\"" << label << "\"}" << endl;
    cerr.flush();
    lastDataReceived = timeNow; // wait before next restart
    stop(timeNow);
    start(timeNow);
    host->requestPollFDRegen();
  }
};
        
void
AlsaMinder::setDemodFMForRaw(bool demod) {
  demodFMForRaw = demod;
};
