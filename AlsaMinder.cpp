#include "AlsaMinder.hpp"

void AlsaMinder::hw_delete_privates() {
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
};

int AlsaMinder::hw_open() {
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

bool AlsaMinder::hw_is_open() {
  return pcm != 0;
};

int AlsaMinder::hw_do_stop() {
  if (pcm) {
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = 0;
  }
  return 0;
};

int AlsaMinder::hw_do_start() {
  if (!pcm && open())
    return 1;
  snd_pcm_prepare(pcm);
  hasError = snd_pcm_start(pcm);
  return 0;
}

int AlsaMinder::hw_do_restart() {
  snd_pcm_recover(pcm, hasError, 1);
  snd_pcm_prepare(pcm);
  snd_pcm_start(pcm);
  return 0;
};

AlsaMinder::AlsaMinder(const string &devName, int rate, unsigned int numChan, const string &label, double now):
  DevMinder(devName, rate, numChan, label, now, BUFFER_FRAMES),
  revents(0),
  pcm(0),
  buffer_frames(BUFFER_FRAMES),
  period_frames(PERIOD_FRAMES)
{
};

AlsaMinder::~AlsaMinder() {
};

int AlsaMinder::hw_getNumPollFDs () {
  return (pcm && shouldBeRunning) ? numFD : 0;
};

int AlsaMinder::hw_getPollFDs (struct pollfd *pollfds) {
  // append pollfd(s) for this object to the specified vector
  // ALSA weirdness means there may be more than one fd per audio device
  if (pcm && shouldBeRunning) {
    if (numFD != snd_pcm_poll_descriptors(pcm, pollfds, numFD)) {
      return 1;
    }
  }
  return 0;
}

int AlsaMinder::hw_handleEvents ( struct pollfd *pollfds, bool timedOut) {
  if (!pcm)
    return 0;
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
    // return number of frames available
    snd_pcm_sframes_t avail = snd_pcm_avail_update (pcm);
    return avail;
  }
  return 0;
};

int AlsaMinder::hw_getFrames (int16_t *buf, int numFrames, double & frameTimestamp) {
  // get most recent period timestamp from ALSA
  snd_htimestamp_t ts;
  snd_pcm_uframes_t av;
  snd_pcm_htimestamp(pcm, &av, &ts);
  frameTimestamp = ts.tv_sec + (double) ts.tv_nsec / 1.0e9 - (double) av / hwRate;

  // begin direct access to ALSA mmap buffers for the device
  const snd_pcm_channel_area_t *areas;
  snd_pcm_uframes_t offset;
  snd_pcm_uframes_t have = (snd_pcm_sframes_t) numFrames;

  int errcode = snd_pcm_mmap_begin (pcm, & areas, & offset, & have);
  if (errcode)
    return errcode > 0 ? -errcode : errcode;

  int16_t *src0, *src1=0; // avoid compiler warning
  int step;

  /*
    copy available samples to buf
  */

  // FIXME: assumes interleaved channels
  src0 = (int16_t *) (((unsigned char *) areas[0].addr) + areas[0].first / 8);
  step = areas[0].step / 16; // FIXME:  hardcoding S16_LE assumption
  src0 += step * offset;

  if (numChan == 2) {
    src1 = (int16_t *) (((unsigned char *) areas[1].addr) + areas[1].first / 8);
    src1 += step * offset;
    for (unsigned i=0; i < have * numChan; ++i) {
      *buf++ = *src0;
      *buf++ = *src1;
      src0 += step;
      src1 += step;
    }
  } else {
    for (unsigned i=0; i < have * numChan; ++i) {
      *buf++ = *src0;
      src0 += step;
    }
  }
  errcode = snd_pcm_mmap_commit (pcm, offset, have);
  if (errcode < 0) {
    std::ostringstream msg;
    msg << "\"event\":\"devProblem\",\"error\":\" snd_pcm_mmap_commit returned with error " << (-errcode) << "\",\"devLabel\":\"" << label << "\"";
    Pollable::asyncMsg(msg.str());
  }
  return (have);
};
