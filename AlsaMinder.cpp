#include "AlsaMinder.hpp"

void AlsaMinder::delete_privates() {
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
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
    
  if ((snd_pcm_open(& pcm, alsaDev.c_str(), SND_PCM_STREAM_CAPTURE, 0))
      || snd_pcm_hw_params_any(pcm, params)
      || snd_pcm_hw_params_set_access_mask(pcm, params, mask)
      || snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE)
      || snd_pcm_hw_params_set_channels(pcm, params, numChan)
      || snd_pcm_hw_params_set_rate(pcm, params, rate, 0)
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

void AlsaMinder::stop(double timeNow) {
  minder->requestPollFDRegen();
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  stopTimestamp = timeNow;
  stopped = true;
};

void AlsaMinder::requestStop(double timeNow) {
  shouldBeRunning = false;
  stop(timeNow);
};

int AlsaMinder::start(double timeNow) {
  if (!pcm && open())
    return 1;
  minder->requestPollFDRegen();
  snd_pcm_prepare(pcm);
  hasError = 0;
  snd_pcm_start(pcm);
  stopped = false;
  startTimestamp = timeNow; // prevent warning about resuming after long pause
  return 0;
}

int AlsaMinder::requestStart(double timeNow) {
  shouldBeRunning = true;
  return start(timeNow);
};

void AlsaMinder::addPluginRunner(std::shared_ptr < PluginRunner > pr) {
  plugins[pr.get()] = pr;
};

void AlsaMinder::removePluginRunner(std::shared_ptr < PluginRunner > pr) {
  // remove plugin runner 
  plugins.erase(pr.get());
};

void AlsaMinder::addRawListener(std::shared_ptr < TCPConnection > conn) {
  rawListeners[conn.get()] = conn;
};

void AlsaMinder::removeRawListener(std::shared_ptr < TCPConnection > conn) {
  rawListeners.erase(conn.get());
};

void AlsaMinder::removeAllRawListeners() {
  rawListeners.clear();
};

AlsaMinder::AlsaMinder(string &alsaDev, int rate, unsigned int numChan, string &label, double now, PollableMinder *minder):
  Pollable(minder),
  alsaDev(alsaDev),
  rate(rate),
  numChan(numChan),
  label(label),
  pcm(0),
  buffer_frames(BUFFER_FRAMES),
  period_frames(PERIOD_FRAMES),
  revents(0),
  totalFrames(0),
  startTimestamp(-1.0),
  stopTimestamp(now),
  shouldBeRunning(false),
  stopped(true),
  hasError(0),
  numFD(0)
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
    << "\"label\":\"" << label << "\","
    << "\"device\":\"" << alsaDev << "\","
    << "\"rate\":" << rate << ","
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
  // ALSA weirdness means there may be more than one fd per fcd
  if (pcm && shouldBeRunning) {
    if (numFD != snd_pcm_poll_descriptors(pcm, pollfds, numFD)) {
      cerr << about() << ": snd_pcm_poll_descriptors returned error." << endl;
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
    minder->requestPollFDRegen();
    stop(timeNow);
    if (start(timeNow)) {
      cerr << "Poll returned with error " << hasError << " on device " << alsaDev << ". Will wait and retry opening.";
    }
    return;
  }
  if (revents & (POLLIN | POLLPRI)) {
    // copy as much data as possible from mmap ring buffer
    // and inform any pluginRunners that we have data

    snd_pcm_sframes_t avail = snd_pcm_avail_update (pcm);
    if (avail < 0) {
      cerr << about() << " with POLLIN|POLLPRI true returned error " << avail << " on snd_pcm_avail_update." << endl;
      snd_pcm_recover(pcm, avail, 1);
      snd_pcm_prepare(pcm);
      hasError = 0;
      snd_pcm_start(pcm);
      startTimestamp = timeNow;

    } else if (avail > 0) {
      lastDataReceived = timeNow;

      long long frameOfTimestamp;
      double frameTimestamp;

      // get most recent period timestamp from ALSA
      snd_htimestamp_t ts;
      snd_pcm_uframes_t av;
      snd_pcm_htimestamp(pcm, &av, &ts);
      frameTimestamp = ts.tv_sec + (double) ts.tv_nsec / 1.0e9;
      frameOfTimestamp = totalFrames + av;
      totalFrames += avail;

      // begin direct access to ALSA mmap buffers for the device
      const snd_pcm_channel_area_t *areas;
      snd_pcm_uframes_t offset;
      snd_pcm_uframes_t have = (snd_pcm_sframes_t) avail;
      if ( snd_pcm_mmap_begin (pcm, & areas, & offset, & have) ) {
        cerr << about() << ": snd_pcm_mmap_begin returned error." << endl;
        return;
      }

      int16_t *src0, *src1=0; // avoid compiler warning
      int step;

      /*
        if a raw output connection exists, queue the new data onto it
      */

      src0 = (int16_t *) (((unsigned char *) areas[0].addr) + areas[0].first / 8);
      step = areas[0].step / 16; // FIXME:  hardcoding S16_LE assumption
      src0 += step * offset;

      for (RawListenerSet::iterator ir = rawListeners.begin(); ir != rawListeners.end(); /**/) {
        // FIXME: big assumptions here:
        // - S16_LE sample format
        // - samples occupy a contiguous area of memory, starting at the first byte
        //   of the first sample on channel 0

        if (auto ptr = (ir->second).lock()) {
          ptr->queueRawOutput((char *) src0, avail * numChan * 2, numChan * 2);
          ++ir;
        } else {
          RawListenerSet::iterator to_delete = ir++;
          rawListeners.erase(to_delete);
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
          ptr->handleData(avail, src0, src1, step, totalFrames, frameTimestamp, frameOfTimestamp);
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
        cerr << about() << ": snd_pcm_mmap_commit returned error." << endl;
      }
    }
  } else if (shouldBeRunning && timeNow - lastDataReceived > MAX_FCD_QUIET_TIME) {
    // this fcd appears to have stopped delivering audio; try restart it
    cerr << about() << " has not sent data for " << timeNow - lastDataReceived << " s; restarting." << endl;
    lastDataReceived = timeNow; // wait before next restart
    stop(timeNow);
    start(timeNow);
    minder->requestPollFDRegen();
  }
};
        
