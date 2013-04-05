/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include "VampAlsaHost.hpp"
#include "Pollable.hpp"
#include "AlsaMinder.hpp"
#include "PluginRunner.hpp"
#include <time.h>

VampAlsaHost::VampAlsaHost():
  regen_pollfds(false),
  have_deferrals(false),
  doing_poll(false)
{
};

VampAlsaHost::~VampAlsaHost() {
  doing_poll = false;
};

void VampAlsaHost::add(std::shared_ptr < Pollable > p) {
  if (! doing_poll) {
    pollables[p.get()->label] = p;
    regen_pollfds = true;
  } else {
    deferred_adds[p.get()->label] = p;
    have_deferrals = true;
  }
}
  
void VampAlsaHost::remove(std::shared_ptr < Pollable > p) {
  if (! doing_poll) {
    pollables.erase(p.get()->label);
    regen_pollfds = true;
  } else {
    deferred_removes[p.get()->label] = p;
    have_deferrals = true;
  }
}

void VampAlsaHost::remove(std::weak_ptr < Pollable > p) {
  remove(p.lock());
};

void VampAlsaHost::remove(Pollable * p) {
  remove(pollables[p->label]);
};

void VampAlsaHost::remove(std::string& label) {
  remove(pollables[label]);
};

Pollable * VampAlsaHost::lookupByName (std::string& label) {
  if (pollables.count(label) == 0)
    return 0;
  return pollables[label].get();
};

std::shared_ptr < Pollable > VampAlsaHost::lookupByNameShared (std::string& label) {
  if (pollables.count(label) == 0)
    return std::shared_ptr < Pollable > ((Pollable *) 0);
  return pollables[label];
};

short& VampAlsaHost::eventsOf(Pollable *p, int offset) {

  // return a reference to the events field for a Pollable.
  // For Pollables with more than one FD, offset can be used
  // to select among them.
  return pollfds[p->indexInPollFD + offset].events;
};

void VampAlsaHost::requestPollFDRegen() {
  regen_pollfds = true;
};

int VampAlsaHost::poll(int timeout) {
  doing_poll = true;

  regenFDs();
  int rv = ::poll(& pollfds[0], pollfds.size(), timeout);
  if (rv < 0) {
    doing_poll = false;
    std::cerr << "poll returned error - vamp-alsa-host" << std::endl;
    return errno;
  }

  bool timedOut = rv == 0;
  // handle events for each pollable.  We give each pollable the chance 
  // to deal with timeouts, by passing that along.

  for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); ++is) {
    auto ptr = is->second.get();
    int i = ptr->indexInPollFD;
    if (i < 0)
      continue;
    ptr->handleEvents(&pollfds[i], timedOut, now());
  }
  doing_poll = false;
  doDeferrals();
  return 0;
};

void VampAlsaHost::doDeferrals() {
  if (! have_deferrals)
    return;
  have_deferrals = false;
  regen_pollfds = true;
  for (PollableSet::iterator is = deferred_removes.begin(); is != deferred_removes.end(); ++is) 
    pollables.erase(is->first);
  deferred_removes.clear();
  for (PollableSet::iterator is = deferred_adds.begin(); is != deferred_adds.end(); ++is) 
    pollables[is->first] = is->second;
  deferred_adds.clear();
};
    
void VampAlsaHost::regenFDs() {
  if (regen_pollfds) {
    regen_pollfds = false;
    pollfds.clear();
    for (PollableSet::iterator is = pollables.begin(); is != pollables.end(); /**/) {
      if (auto ptr = is->second.get()) {
        int where = pollfds.size();
        int numFDs = ptr->getNumPollFDs();
        if (numFDs > 0) {
          ptr->indexInPollFD = where;
          pollfds.resize(where + numFDs);
          ptr->getPollFDs(& pollfds[where]);
        } else {
          ptr->indexInPollFD = -1;
        }
        ++is;
      } else {
        auto to_delete = is;
        ++is;
        pollables.erase(to_delete->first);
      }
    }
  }
}

string VampAlsaHost::runCommand(string cmdString, string connLabel) {
  ostringstream reply;
  string word;
  istringstream cmd(cmdString);
  double realTimeNow = now(false);
  if (! (cmd >> word))
    return reply.str();
  if (word == "stopAll") {
    // quick stop of all devices
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 50 * 1000 * 1000;
    for (PollableSet::iterator ips = pollables.begin(); ips != pollables.end(); ++ips) {
      ips->second->stop(realTimeNow);
      // sleep 50 ms between stops
      nanosleep(&t, 0);
    }
    nanosleep (&t, 0);
    reply << "{\"message\":\"All devices stopped.\"}\n";
    requestPollFDRegen();
  } else if (word == "startAll") {
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 500 * 1000 * 1000;
    for (PollableSet::iterator ips = pollables.begin(); ips != pollables.end(); ++ips) {
      ips->second->start(realTimeNow);
      // sleep 500 ms between starts
      nanosleep(&t, 0);
    }
    reply << "{\"message\":\"All devices started.\"}\n";
    requestPollFDRegen();
  } else if (word == "status") {
    string label;
    cmd >> label;
    Pollable *p = lookupByName(label);
    if (p) {
      reply << p->toJSON() << '\n';
    } else {
      reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}\n";
    }
  } else if (word == "list") {
    reply << "{";
    int i = pollables.size();
    for (PollableSet::iterator ips = pollables.begin(); ips != pollables.end(); ++ips, --i) {
      reply << "\"" << ips->second->label << "\":" << ips->second->toJSON() << (i > 1 ? "," : "");
    }
    reply << "}\n";
  } else if (word == "start" || word == "stop") {
    string label;
    cmd >> label;
    bool doStop = word == "stop";
    Pollable *p = lookupByName(label);
    if (p) {
      if (doStop)
        p->stop(realTimeNow);
      else
        p->start(realTimeNow);
      reply << p->toJSON() << '\n';
      requestPollFDRegen();
    } else {
      reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}\n";
    }
  } else if (word == "rawOn" || word == "rawOff" || word == "rawNone") {
    string label;
    cmd >> label;
    unsigned long long framesBetweenTimestamps = 0;
    cmd >> framesBetweenTimestamps;

    AlsaMinder *p = dynamic_cast < AlsaMinder * > (lookupByName(label));
    if (p) {
      if (word == "rawOn") {
        p->addRawListener(connLabel, framesBetweenTimestamps);
      } else if (word == "rawOff") {
        p->removeRawListener(connLabel);
      } else {
        p->removeAllRawListeners();
      }
    } else {
      reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}\n";
    }
  } else if (word == "fmOn" || word == "fmOff") {
    string label;
    cmd >> label;
    AlsaMinder *p = dynamic_cast < AlsaMinder * > (lookupByName(label));
    if (p) {
      p->setDemodFMForRaw(word == "fmOn");
    } else {
      reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}\n";
    }
  } else if (word == "open" ) {
    string label, alsaDev;
    int rate, numChan;
    cmd >> label >> alsaDev >> rate >> numChan;
    try {
      std::shared_ptr < AlsaMinder > ptr = std::make_shared < AlsaMinder > (alsaDev, rate, numChan, label, realTimeNow, this);
      add(ptr);
      reply << ptr->toJSON() << '\n';
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "close") {
    string label;
    cmd >> label;
    AlsaMinder *dev = dynamic_cast < AlsaMinder * > (lookupByName(label));
    if (dev) {
      dev->stop(realTimeNow);
      reply << dev->toJSON() << '\n';
      remove(label);
    } else {
      reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}\n";
    }
    requestPollFDRegen();
  } else if (word == "attach") {
    string devLabel, pluginLabel, pluginLib, pluginName, outputName;
    string par;
    float val;
    ParamSet ps;
    cmd >> devLabel >> pluginLabel >> pluginLib >> pluginName >> outputName;
    for (;;) {
      if (! (cmd >> par >> val))
        break;
      ps[par] = val;
    }
    try {
      AlsaMinder *dev = dynamic_cast < AlsaMinder * > (lookupByName(devLabel));
      if (!dev)
        throw std::runtime_error(string("There is no device with label '") + devLabel + "'");
      if (lookupByName(pluginLabel))
        throw std::runtime_error(string("There is already a device or plugin with label '") + pluginLabel + "'");
      std::shared_ptr < PluginRunner > plugin = std::make_shared < PluginRunner > (pluginLabel, devLabel, dev->rate, dev->hwRate, dev->numChan, pluginLib, pluginName, outputName, ps, this);
      pollables[pluginLabel] = static_pointer_cast < Pollable > (plugin);
      dev->addPluginRunner(plugin);
      if (! plugin->addOutputListener(defaultOutputListener))
        // the default output listener doesn't seem to exist any longer
        // so reset its name in case a subsequent connection has the same label
        defaultOutputListener = "";
      reply << plugin->toJSON() << '\n';
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "param") {
    string pluginLabel;
    ParamSet ps;
    string par;
    float val;
    cmd >> pluginLabel;
    for (;;) {
      if (! (cmd >> par >> val))
        break;
      ps[par] = val;
    }
    try {
      PollableSet::iterator ip = pollables.find(pluginLabel);
      if (ip == pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      std::shared_ptr < PluginRunner > p = dynamic_pointer_cast < PluginRunner > (ip->second);
      auto ptr = p.get();
      if (ptr)
        p->setParameters(ps);
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "detach") {
    string pluginLabel;
    cmd >> pluginLabel;
    try {
      PollableSet::iterator ip = pollables.find(pluginLabel);
      if (ip == pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      pollables.erase(ip);
      reply << "{\"message\": \"Plugin " << pluginLabel << " has been detached.\"}\n";
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "receive") {
    string pluginLabel;
    cmd >> pluginLabel;
    try {
      PollableSet::iterator ip = pollables.find(pluginLabel);
      if (ip == pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      std::shared_ptr < PluginRunner > p = dynamic_pointer_cast < PluginRunner > (ip->second);
      auto ptr = p.get();
      if (ptr)
        ptr->addOutputListener(connLabel);
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "receiveAll") {
    for (PollableSet::iterator ip = pollables.begin(); ip != pollables.end(); ++ip) {
      std::shared_ptr < PluginRunner > p = dynamic_pointer_cast < PluginRunner > (ip->second);
      auto ptr = p.get();
      if (ptr)
        ptr->addOutputListener(connLabel);
      defaultOutputListener = connLabel;
    }
  } else if (word == "quit" ) {
    reply << "{\"message\": \"Terminating server.\"}\n";
    throw std::runtime_error("Quit by client.\n");
  } else if (word == "help" ) {
    reply <<  "Commands:\n" << VampAlsaHost::commandHelp << endl; // NB: we don't use JSON for this
  } else {
    reply << "{\"error\": \"Error: invalid command\"}\n";
  }
  return reply.str();
};


int VampAlsaHost::run()
{
  int rv;
  do {
    rv = poll(2000); // 2 second timeout
  } while (! rv);
  return rv;
}

double
VampAlsaHost::now(bool is_monotonic) {
  struct timespec clockTime;
  clock_gettime(is_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME, &clockTime);
  return clockTime.tv_sec + clockTime.tv_nsec / (double) 1.0e9;
};

const string 
VampAlsaHost::commandHelp =
          "       open DEV_LABEL AUDIO_DEV RATE NUM_CHANNELS\n"
          "          Opens an audio device so that plugins can be attached to it.\n"
          "          To start processing, you must attach a plugin and start the device\n"
          "          using the 'start DEV_LABEL' command - see below\n\n"
          "          Arguments:\n"
          "          DEV_LABEL: a label which will identify this audio device in subsequent commands\n"
          "             and in output lines.  This must not already be a label of another device\n"
          "             or a plugin instance (see below).\n"
          "          AUDIO_DEV: the ALSA name of the audio device (e.g. 'default:CARD=V10')\n"
          "          RATE: the sampling rate to use for the device (e.g. 48000)\n"
          "          NUM_CHANNELS: the number of channels to read from the device (usually 1 or 2)\n\n"
          "          e.g. open 3 default:CARD=V10_2 48000 2\n\n"

          "       attach DEV_LABEL PLUGIN_LABEL PLUGIN_SONAME PLUGIN_ID PLUGIN_OUTPUT [PAR VALUE]*\n"
          "          Load the specified plugin and attach it to the specified audio device.  Multiple plugins\n"
          "          can be attached to the same device.  All incoming data is sent to all attached\n"
          "          plugins, in the same order in which they were attached.\n"
          "          DEV_LABEL: the label for the input device, which must already have been opened with open\n"
          "          PLUGIN_LABEL: the label for this plugin instance, for use in subsequent commands.\n"
          "          This label must not already be the label of a device or another plugin instance.\n"
          "          PLUGIN_SONAME: the name (without path) of the library containing the plugin\n"
          "          PLUGIN_ID: the name of the plugin within the library\n"
          "          PLUGIN_OUTPUT: the name of the desired output from the plugin\n"
          "                         (some plugins have multiple outputs - you must pick one)\n"
          "          [PAR VALUE]: an optional set of plugin parameter settings, where:\n"
          "                       PAR: is the name of a plugin parameter\n"
          "                       VALUE: is the value to be assiged to the parameter\n\n"
          "          e.g. attach 3 pulse3 lotek-plugins.so findpulsefdbatch pulses minsnr 6\n\n"
          "          Output from the plugin will be sent to any TCP connection\n"
          "          which has issued a corresponding 'receive' or 'receiveAll' command, or\n"
          "          discarded if no 'receive' connection exists.\n\n"

          "       param PLUGIN_LABEL [PAR VALUE]*\n"
          "          set the value(s) of specified parameter(s) of given attached plugin instance.\n"
          "          PLUGIN_LABEL: label of an attached plugin instance.\n\n"
          "          PAR: the name of a plugin parameter\n"
          "          VALUE: the value to assign to the parameter\n\n"
          "          e.g. setpar pulse3 minsnr 3\n\n"

          "       detach PLUGIN_LABEL\n"
          "          Stop sending data to the specified plugin instance, and delete it.  Any other\n"
          "          instances of the same plugin, and any other plugins attached to the same device\n"
          "          are not affected.\n"
          "          PLUGIN_LABEL: the label of an attached plugin instance.\n\n"

          "       receive PLUGIN_LABEL\n"
          "          Start sending any output for the specified plugin to the TCP connection from\n"
          "          which this command is issued.  This does not affect any existing connections already\n"
          "          set to receive the output, so multiple connections can receive output from the same\n"
          "          attached plugin.\n"
          "          PLUGIN_LABEL: the label for an attached plugin instance.\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"

          "       receiveAll\n"
          "          Start sending any output data for all currently attached plugins to the TCP connection from\n"
          "          which this command is issued.  Also, any plugins attached after this command is issued\n"
          "          will also send output to the issuing TCP connection, unless a subsequent receiveAll command\n"  
          "          is issued from a different TCP connection.  This command does not affect any existing\n"
          "          connections already receiving data from an attached plugin.\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"

          "       rawOn DEV_LABEL [FRAMES_BETWEEN_TIMESTAMPS]\n"
          "          Raw data from the device DEV_LABEL will be sent to the issuing TCP connection\n"
          "          once the device is started, independently of any plugin processing.\n\n"
          "          FRAMES_BETWEEN_TIMESTAMPS: if specified, vamp-alsa-host will emit timestamps\n"
          "          to the raw output stream.  Each timestamp is an 8-byte double (little-endian)\n"
          "          giving the precise timestamp of the frame which immediately follows.\n"
          "          A timestamp is emitted at the start of the raw stream, and then after each\n"
          "          sequence of FRAMES_BETWEEN_TIMESTAMPS frames.\n\n"
          "          Note: this command does not return a reply unless there is an error.\n"
    
          "       rawOff DEV_LABEL\n"
          "          Stop sending raw data from the device DEV_LABEL to the issuing TCP connection.\n\n"
          "          Note: this command does not return a reply unless there is an error.\n"

          "       rawNone DEV_LABEL\n"
          "          Do not send raw data from the device DEV_LABEL to *any* TCP connections.\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"
    
          "       fmOn DEV_LABEL\n"
          "          Specify that raw data from the device DEV_LABEL will be FM-demodulated\n"
          "          before being sent to to any TCP connections which are listening to it via\n"
          "          a rawOn command.  This does not change the data seen by plugins.\n"
          "          Also, this command does not by itself cause raw data to be emitted; you\n"
          "          must use rawOn command on a connection for this command to have any effect.\n"
          "          Finally, this command only has effect on a stereo device, and will reduce\n"
          "          the raw output to a single channel.  This command is likely only to be\n"
          "          useful for radio receivers which output I/Q as stereo audio channels.\n\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"
    
          "       fmOff DEV_LABEL\n"
          "          Turn off FM-demodulation for the specified device;  raw data will be sent\n"
          "          to any listening TCP connections as-is.\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"

          "       start DEV_LABEL\n"
          "          Begin acquiring data from the audio device identified by DEV_LABEL.\n"
          "          This must already have been created using an 'open' command.\n"
          "          Data are read from device LABEL, and passed to any attached plugins.\n"
          "          Output from each plugin instance is sent to the socket from which the\n"
          "          corresponding 'attach' command was issued.\n\n"

          "       stop DEV_LABEL\n"
          "          Stop acquiring data from the audio device identified by DEV_LABEL.\n\n"

          "Note: start and stop commands can be sent repeatedly for a device which has been opened.\n"
          "The associated plugin will see a continuous stream of data, albeit with timestamps\n"
          "reflecting the loss of data between stop and (re)start.  The same applies to startAll and stopAll.\n"
          "To re-start a plugin completely, you must close the device and re-open it.\n\n"

          "       close DEV_LABEL\n"
          "           Stop acquiring data from the device and shut it down.  This deletes\n"
          "           the device from the server, so that DEV_LABEL cannot be used in subsequent commands\n"
          "           until another 'open DEV_LABEL...' command is sent.\n\n"

          "       status LABEL\n"
          "           Report on the status of the audio device identified by LABEL\n"
          "           The reply is a JSON object.\n\n"

          "       pstatus LABEL\n"
          "           Report on the status of plugin identified by LABEL\n"
          "           The reply is a JSON object.\n\n"

          "       stopAll\n"
          "           Stop all devices, e.g. to allow changing settings on upstream devices.\n"
        
          "       startAll\n"
          "           (Re-)start all devices (e.g. after a stopAll command).\n\n"
        
          "       list\n"
          "           Return the status of all open audio devices and plugins.\n\n"

          "       help\n"
          "           Print this information.\n\n"

          "       quit\n"
          "           Close all open devices and quit the program.\n";
