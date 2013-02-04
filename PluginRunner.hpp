#ifndef PLUGINRUNNER_HPP
#define PLUGINRUNNER_HPP

#include <vamp-hostsdk/PluginHostAdapter.h>
#include <vamp-hostsdk/PluginInputDomainAdapter.h>
#include <vamp-hostsdk/PluginBufferingAdapter.h>
#include <vamp-hostsdk/PluginLoader.h>
#include <vamp-sdk/Plugin.h>
#include <vamp-sdk/PluginBase.h>
#include <stdint.h>
#include <set>

using namespace Vamp;
using namespace Vamp::HostExt;

#include "ParamSet.hpp"
#include "TCPConnection.hpp"

class AlsaMinder;

class PluginRunner {
public:
  string             label;            // name of this plugin runner (used in commands)
  string             devLabel;         // label of device from which plugin receives input
  string             pluginSOName;     // name of shared object where plugin resides
  string             pluginID;         // id of plugin
  string             pluginOutput;     // name of output to obtain from plugin
  ParamSet           pluginParams;     // parameter settings for plugin

protected:
  static PluginLoader *pluginLoader;   // plugin loader (singleton)
  int                numChan;          // number of channels plugin uses
  int                rate;             // sampling rate; frames per second
  long long          totalFeatures;    // total number of "features" (e.g. lotek pulses) seen on this FCD
  Plugin *           plugin;           // VAMP plugin we'll be running on this fcd
  float **           plugbuf;          // pointer to one buffer for each channel (left, right) of float data for plugin
  int                outputNo;         // index of plugin output corresponding to pluginOutput
  int                blockSize;        // size (in frames) of blocks sent to plugin
  int                stepSize;         // amount (in frames) by which consecutive blocks differ
  int                framesInPlugBuf;  // number of frames in plugin buffers since last call to plugin->process()
  bool               isOutputBinary;   // if true, output from plugin is not text.  For text outputs, if
  // the output buffer gets filled before it can be written to a socket,
  // the oldest output is discarded line by line, so that any output line
  // is either completely written or not written at all.  For binary output,
  // we just discard at arbitrary boundaries.

  TCPConnection *    outputConnection; // connection to which output is written

public:
  PluginRunner(string &label, string &devLabel, int rate, int numChan, string &pluginSOName, string &pluginID, string &pluginOutput, ParamSet &ps, TCPConnection *outputConnection);
  ~PluginRunner();
  int loadPlugin(ParamSet &ps);
  void handleData(long avail, int16_t *src0, int16_t *src1, int step, long long totalFrames, long long frameOfTimestamp, double frameTimestamp);
  void outputFeatures(Plugin::FeatureSet features, string prefix);
  string toJSON();

private:
  void delete_privates();
};

typedef std::set < std::weak_ptr < PluginRunner >, std::owner_less<std::weak_ptr< PluginRunner > > > PluginRunnerSet;

#include "AlsaMinder.hpp"

#endif // PLUGINRUNNER_HPP
