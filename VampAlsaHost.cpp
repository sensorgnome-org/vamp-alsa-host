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

void VampAlsaHost::poll(int timeout) {
  doing_poll = true;

  regenFDs();
  int rv = ::poll(& pollfds[0], pollfds.size(), timeout);
  if (rv < 0) {
    doing_poll = false;
    return;
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
  return;
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
        reply << "{\"message\":\"All devices stopped.\"}";
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
        reply << "{\"message\":\"All devices started.\"}";
        requestPollFDRegen();
    } else if (word == "status") {
        string label;
        cmd >> label;
        Pollable *p = lookupByName(label);
        if (p) {
            reply << p->toJSON();
        } else {
            reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}";
        }
    } else if (word == "list") {
        reply << "{";
        int i = pollables.size();
        for (PollableSet::iterator ips = pollables.begin(); ips != pollables.end(); ++ips, --i) {
            reply << "\"" << ips->second->label << "\":" << ips->second->toJSON() << (i > 1 ? "," : "");
        }
        reply << "}";
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
            reply << p->toJSON();
            requestPollFDRegen();
        } else {
            reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}";
        }
    } else if (word == "rawOn" || word == "rawOff" || word == "rawNone") {
        string label;
        cmd >> label;
        AlsaMinder *p = dynamic_cast < AlsaMinder * > (lookupByName(label));
        if (p) {
            if (word == "rawOn") {
                p->addRawListener(connLabel);
            } else if (word == "rawOff") {
                p->removeRawListener(connLabel);
            } else {
                p->removeAllRawListeners();
            }
        } else {
            reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}";
        }
    } else if (word == "open" ) {
        string label, alsaDev;
        int rate, numChan;
        cmd >> label >> alsaDev >> rate >> numChan;
        try {
            std::shared_ptr < AlsaMinder > ptr = std::make_shared < AlsaMinder > (alsaDev, rate, numChan, label, realTimeNow, this);
            add(ptr);
            reply << ptr->toJSON();
        } catch (std::runtime_error e) {
            reply << "{\"error\": \"Error:" << e.what() << "\"}";
        };
    } else if (word == "close") {
        string label;
        cmd >> label;
        AlsaMinder *dev = dynamic_cast < AlsaMinder * > (lookupByName(label));
        if (dev) {
            dev->stop(realTimeNow);
            reply << dev->toJSON();
            remove(label);
        } else {
            reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}";
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
            std::shared_ptr < PluginRunner > plugin = std::make_shared < PluginRunner > (pluginLabel, devLabel, dev->rate, dev->numChan, pluginLib, pluginName, outputName, ps, lookupByNameShared(connLabel), this);
            pollables[pluginLabel] = static_pointer_cast < Pollable > (plugin);
            dev->addPluginRunner(plugin);
            reply << plugin->toJSON();
        } catch (std::runtime_error e) {
            reply << "{\"error\": \"Error:" << e.what() << "\"}";
        };
    } else if (word == "detach") {
        string pluginLabel;
        cmd >> pluginLabel;
        try {
            PollableSet::iterator ip = pollables.find(pluginLabel);
            if (ip == pollables.end())
                throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
            pollables.erase(ip);
            reply << "{\"message\": \"Plugin " << pluginLabel << " has been detached.\"}";
        } catch (std::runtime_error e) {
            reply << "{\"error\": \"Error:" << e.what() << "\"}";
        };
    } else if (word == "quit" ) {
        reply << "{\"message\": \"Terminating server.\"}";
        throw std::runtime_error("Quit by client.\n");
    } else if (word == "help" ) {
      reply <<  "Commands:\n" << VampAlsaHost::commandHelp << endl; // NB: we don't use JSON for this
    } else {
        reply << "{\"error\": \"Error: invalid command\"}";
    }
    reply << endl;
    return reply.str();
};


void VampAlsaHost::run()
{
    cout << setprecision(3);

    for (;;) {
        poll(10000);
    }
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
    "          Output from the plugin will be sent to the TCP connection which issued the 'attach' command\n\n"

    "       detach PLUGIN_LABEL\n"
    "          Stop sending data to the specified plugin instance, and delete it.  Any other\n"
    "          instances of the same plugin, and any other plugins attached to the same device\n"
    "          are not affected.\n"
    "          PLUGIN_LABEL: the label for an attached plugin instance.\n\n"

    "       rawOn DEV_LABEL\n"
    "          Raw data from the device DEV_LABEL will be sent to the issuing TCP connection\n"
    "          once the device is started, independently of any plugin processing.\n\n"
    
    "       rawOff DEV_LABEL\n"
    "          Stop sending raw data from the device DEV_LABEL to the issuing TCP connection.\n\n"

    "       rawNone DEV_LABEL\n"
    "          Do not send raw data from the device DEV_LABEL to *any* TCP connections.\n\n"
    
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
