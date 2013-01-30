#include "PluginRunner.hpp"

void PluginRunner::delete_privates() {
  if (inputSource)
    inputSource->removePlugin(this);
  if (plugin) {
    delete plugin;
  }
  if (plugbuf) {
    for (unsigned i=0; i < numChan; ++i) {
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

  // make sure the plugin is compatible: it must run in the time domain and accept 2 channels
        
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

  ParameterList plist = plugin->getParameterDescriptors();
  for (ParameterList::iterator ipa = plist.begin(); ipa != plist.end(); ++ipa) {
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

PluginRunner::PluginRunner(string &label, string &pluginSOName, string &pluginID, string &pluginOutput, ParamSet &ps, double timeNow):
  label(label),
  pluginSOName(pluginSOName),
  pluginID(pluginID),
  pluginOutput(pluginOutput),
  pluginParams(ps),
  totalFeatures(0),
  plugin(0),
  plugbuf(0),
  framesInPlugBuf(0),
  outputNo(-1),
  blockSize(0),
  stepSize(0),
  inputSource(0),
  isOutputBinary(false),
  bytesInBuffer(0),
  outputFD(-1)
{
  if (open()) {
    // there was an error, so throw an exception
    delete_privates();
    throw std::runtime_error("Could not open audio device or could not set required parameters");
  }

  // try load the plugin and throw if we fail

  if (loadPlugin(ps)) {
    delete_privates();
    throw std::runtime_error("Could not load plugin or plugin is not compatible");
  }

  stopTimestamp = now();  // mark time device was opened
};

PluginRunner::~PluginRunner() {
  delete_privates();
};

void PluginRunner::setInputSource(AlsaMinder *am) {
  inputSource = am;
  am->addPlugin(this);
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
      getFeaturesToBuffer(plugin->process(plugbuf, rt), label));

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
PluginRunner::getFeaturesToBuffer(Plugin::FeatureSet features, string prefix)
{
  ostringstream txt;
  *totalFeatures += features[outputNo].size();
  int available = OUTPUT_BUFFER_SIZE - bytesInBuffer;
  for (Plugin::FeatureList::iterator f = features[outputNo].begin(), g = features[outputNo].end(); f != g; ++f ) {
    if (isOutputBinary) {
      // copy values as raw bytes
      int needed = std::min(OUTPUT_BUFFER_SIZE, values.length * sizeof(float));
      int shortby = needed - available;
      if (shortby > 0) {
        // discard old data
        memmove(& outputBuffer, & outputBuffer[shortBy], bytesInBuffer - shortBy);
        bytesInBuffer -= shortBy;
      }
      char * src = (char *) (f->values[0]);
      memcpy(& outputBuffer[bytesInBuffer], src, needed);
      bytesInBuffer += needed;
      available -= needed;
    } else {
      RealTime rt;

      if (f->hasTimestamp) {
        rt = f->timestamp;
      }

      if (prefix.length())
        txt << prefix << ",";
      txt << setprecision(14);
      txt << (double) (rt.sec + rt.nsec / (double) 1.0e9);
      txt << setprecision(3);

      if (f->hasDuration) {
        rt = f->duration;
        txt << "," << rt.toString();
      }

      for (std::vector<float>::iterator v = f->values.begin(), w=f->values.end(); v != w; ++v) {
        txt << "," << *v;
      }

      txt << endl;
    }
  }
  if (! isOutputBinary) {
    // add strings to buffer, but any discard of old data must be entire lines
    int size = txt.tellp();
    int needed = std::min(OUTPUT_BUFFER_SIZE, size());
    int shortby = needed - available;
    char *firstToUse = outputBuffer;
    while (shortby > 0) {
      // advance to start of next line
      char *endOfFirstLine = strchr(firstToUse, '\n');
      if (! endOfFirstLine)
        break;
      shortby -= endOfFirstLine - firstToUse + 1;
      firstToUse = endOfFirstLine + 1;
    }
    int drop = firstToUse - & outputBuffer[0];
    if (drop > 0) {
      bytesInBuffer -= drop;
      memcpy(& outputBuffer[0], &outputBuffer[drop], bytesInBuffer);
    }
    memcpy(& outputBuffer[bytesInBuffer], txt.str().c_str(), needed);
    bytesInBuffer += size;
  }
}


