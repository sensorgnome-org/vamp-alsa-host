#include "PluginRunner.hpp"

void PluginRunner::delete_privates() {
  if (plugin) {
    delete plugin;
  }
  if (plugbuf) {
    for (unsigned int i=0; i < numChan; ++i) {
      if (plugbuf[i])
        delete [] plugbuf[i];
    }
    delete [] plugbuf;
  }
};

int PluginRunner::loadPlugin(ParamSet &ps) {
  // load the plugin, make sure it is compatible and that all parameters are okay.

  // get an instance of the plugin loader, 

  if (! pluginLoader)
    pluginLoader =  PluginLoader::getInstance();

  PluginLoader::PluginKey key = pluginLoader->composePluginKey(pluginSOName, pluginID);

  plugin = pluginLoader->loadPlugin (key, rate, 0); // no adapting, rather than PluginLoader::ADAPT_ALL_SAFE;

  if (! plugin) {
    return 1;
  }

  // make sure the plugin is compatible: it must run in the time domain and accept an appropriate number of channels
        
  if (plugin->getInputDomain() != Plugin::TimeDomain
      || plugin->getMinChannelCount() > numChan
      || plugin->getMaxChannelCount() < numChan) {
    return 2;
  }

  // get preferred block and step sizes, and do sanity check

  blockSize = plugin->getPreferredBlockSize();
  stepSize = plugin->getPreferredStepSize();
        
  if (blockSize == 0) {
    blockSize = 1024;
  }
  if (stepSize == 0) {
    stepSize = blockSize;
  } else if (stepSize > blockSize) {
    blockSize = stepSize;
  }

  // allocate buffers to transfer float audio data to plugin

  plugbuf = new float*[numChan];
  for (unsigned c = 0; c < numChan; ++c)
    plugbuf[c] = new float[blockSize + 2];  // FIXME: is "+2" only to leave room for DFT?

  // make sure the named output is valid
        
  Plugin::OutputList outputs = plugin->getOutputDescriptors();
        
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (outputs[i].identifier == pluginOutput) {
      outputNo = i;
      break;
    }
  }

  if (outputNo < 0) {
    return 3;
  }

  // set the plugin's parameters

  for (ParamSetIter it = ps.begin(); it != ps.end(); ++it)
    plugin->setParameter(it->first, it->second);

  // initialise the plugin

  if (! plugin->initialise(numChan, stepSize, blockSize)) {
    return 4;
  }

  // if the plugin has a parameter named isForVampAlsaHost, set it to 1.  This allows
  // a plugin to e.g. produce different output depending on whether it is run with
  // vamp-alsa-host or audacity (e.g. FindLotekPulse generates gap size labels when
  // run with audacity, not with vamp-alsa-host)

  // if the plugin has a parameter named isOutputBinary that is quantized with one value
  // then set isOutputBinary to true.  This means we don't have to discard old buffered
  // output line-by-line as we would with text.

  // if the plugin has a parameter named maxBinaryOutputSize that is quantized with one
  // value, then set MAX_BUFFER_SIZE to that value.  Output from each call to the plugin's
  // process() method is guaranteed to be no larger than MAX_BUFFER_SIZE bytes.

  PluginBase::ParameterList plist = plugin->getParameterDescriptors();
  for (PluginBase::ParameterList::iterator ipa = plist.begin(); ipa != plist.end(); ++ipa) {
    if (ipa->identifier == "isForVampAlsaHost") {
      plugin->setParameter(ipa->identifier, 1.0);
    } else if (ipa->identifier == "isOutputBinary" && ipa->isQuantized &&
               ipa->minValue == ipa->maxValue) {
      isOutputBinary = true;
    } else if (ipa->identifier == "maxBinaryOutputSize" && ipa->isQuantized &&
               ipa->minValue == ipa->maxValue) {
      isOutputBinary = true;
    }

  }
        
  return 0;
};

PluginRunner::PluginRunner(string &label, string &devLabel, int rate, int numChan, string &pluginSOName, string &pluginID, string &pluginOutput, ParamSet &ps, std::shared_ptr < Pollable > outputConnection, VampAlsaHost *host):
  Pollable (host, label),
  label(label),
  devLabel(devLabel),
  pluginSOName(pluginSOName),
  pluginID(pluginID),
  pluginOutput(pluginOutput),
  pluginParams(ps),
  host(host),
  rate(rate),
  numChan(numChan),
  totalFeatures(0),
  plugin(0),
  plugbuf(0),
  outputNo(-1),
  blockSize(0),
  stepSize(0),
  framesInPlugBuf(0),
  isOutputBinary(false)
{

  // try load the plugin and throw if we fail

  if (loadPlugin(ps)) {
    delete_privates();
    throw std::runtime_error("Could not load plugin or plugin is not compatible");
  }
  this->outputConnection = static_pointer_cast < TCPConnection > (outputConnection);
};

PluginRunner::~PluginRunner() {
  delete_privates();
};

void PluginRunner::handleData(snd_pcm_sframes_t avail, int16_t *src0, int16_t *src1, int step, long long totalFrames, long long frameOfTimestamp, double frameTimestamp) {
  // alsaHandler has some data for us.  If src1 is NULL, it's only one channel; otherwise it's two channels

  while (avail > 0) {
    int to_copy = std::min((int) avail, blockSize - framesInPlugBuf);
    float *pb0, *pb1;

    pb0 = plugbuf[0] + framesInPlugBuf;

    // choose an inner loop, depending on number of channels
    if (src1) {
      // two channels
      pb1 = plugbuf[1] + framesInPlugBuf;
      for (float *pbe = pb0 + to_copy; pb0 != pbe; /**/ ) {
        *pb0++ = *src0 / 32768.0;
        *pb1++ = *src1 / 32768.0;
        src0 += step;
        src1 += step;
      }
    } else {
      // one channel
      for (float *pbe = pb0 + to_copy; pb0 != pbe; /**/) {
        *pb0++ = *src0 / 32768.0;
        src0 += step;
      }
    }
    framesInPlugBuf += to_copy;
    avail -= to_copy;
    if (framesInPlugBuf == blockSize) {
      // time to call the plugin

      RealTime rt = RealTime::fromSeconds( frameTimestamp + (totalFrames - blockSize - frameOfTimestamp) / (double) rate);
      outputFeatures(plugin->process(plugbuf, rt), label);

    // shift samples if we're not advancing by a full
    // block.
    // Too bad the VAMP specs don't let the
    // process() function deal with two segments for each
    // buffer; then we wouldn't need these wastefull calls
    // to memmove!  MAYBE FIXME: fake this by changing our own
    // plugin to have blockSize = stepSize and deal
    // internally with handling overlap!  Then fix this
    // code so copying from alsa's mmap segment is done in
    // one pass for all plugins waiting on a device, then
    // the mmap segment is marked as available, then
    // another pass calls process() on all plugins with
    // full buffers.

    if (stepSize < blockSize) {
      memmove(&plugbuf[0][0], &plugbuf[0][stepSize], (blockSize - stepSize) * sizeof(float));
      if (src1)
        memmove(&plugbuf[1][0], &plugbuf[1][stepSize], (blockSize - stepSize) * sizeof(float));
      framesInPlugBuf = blockSize - stepSize;
    } else {
      framesInPlugBuf = 0;
    }
  }
}
};

void
PluginRunner::outputFeatures(Plugin::FeatureSet features, string prefix)
{
  auto ptr = outputConnection.lock();
  if (! ptr) {
    host->remove(label);
    return;
  }
  
  totalFeatures += features[outputNo].size();
  for (Plugin::FeatureList::iterator f = features[outputNo].begin(), g = features[outputNo].end(); f != g; ++f ) {
    if (isOutputBinary) {
      // copy values as raw bytes
      ptr->queueFloatOutput(f->values);
    } else {
      ostringstream txt;
      txt.setf(ios::fixed,ios::floatfield);
      txt.precision(4); // 0.1 ms precision for timestamp

      RealTime rt;

      if (f->hasTimestamp) {
        rt = f->timestamp;
      }

      if (prefix.length())
        txt << prefix << ",";
      txt << (double) (rt.sec + rt.nsec / (double) 1.0e9);
      txt.unsetf(ios::floatfield); // now 4 digits total precision

      if (f->hasDuration) {
        rt = f->duration;
        txt << "," << rt.toString();
      }

      for (std::vector<float>::iterator v = f->values.begin(), w=f->values.end(); v != w; ++v) {
        txt << "," << *v;
      }

      txt << endl;
      ptr->queueTextOutput(txt.str());
    }
  }
};

string PluginRunner::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"label\":\"" << label << "\","
    << "\"type\":\"PluginRunner\","
    << "\"devLabel\":\"" << devLabel << "\","
    << "\"library\":\"" << pluginSOName << "\","
    << "\"ID\":" << pluginID << ","
    << "\"output\":" << pluginOutput << ","
    << "\"totalFeatures\":" << totalFeatures
    << "}";
  return s.str();
}

PluginLoader *PluginRunner::pluginLoader = 0;

/*
  Trivially implementing the following methods allow us to put
  PluginRunners in the same host container as TCPListeners,
  TCPConnections, and AlsaMinders.  It's an ugly design, but I
  couldn't think of a better one, and it makes for simpler code, as
  far as I can tell.  */

void PluginRunner::stop(double timeNow) {
  /* do nothing */
};

int
PluginRunner::start(double timeNow) {
  /* do nothing */
  return 0;
};

int
PluginRunner::getNumPollFDs() {
  /* do nothing */
  return 0;
};

int 
PluginRunner::getPollFDs (struct pollfd * pollfds) {
  /* do nothing */
  return 0;
};

void
PluginRunner::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow)
{
  /* do nothing */
};
