/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include "VampAlsaHost.hpp"
#include "Pollable.hpp"
#include "DevMinder.hpp"
#include "PluginRunner.hpp"
#include "WavFileWriter.hpp"
#include <time.h>

VampAlsaHost::VampAlsaHost()
{
};

VampAlsaHost::~VampAlsaHost() {
};


string VampAlsaHost::runCommand(string cmdString, string connLabel) {
  ostringstream reply;
  string word;
  istringstream cmd(cmdString);
  double realTimeNow = now(false);

  if (! (cmd >> word))
    return reply.str();

  if (word == "stopAll") {
    // quick stop of all devices
    for (PollableSet::iterator ips = Pollable::pollables.begin(); ips != Pollable::pollables.end(); ++ips) {
      ips->second->stop(realTimeNow);
    }
    reply << "{\"message\":\"All devices stopped.\"}\n";
    Pollable::requestPollFDRegen();
  } else if (word == "startAll") {
    for (PollableSet::iterator ips = Pollable::pollables.begin(); ips != Pollable::pollables.end(); ++ips) {
      ips->second->start(realTimeNow);
    }
    reply << "{\"message\":\"All devices started.\"}\n";
    Pollable::requestPollFDRegen();
  } else if (word == "status") {
    string label;
    cmd >> label;
    Pollable *p = Pollable::lookupByName(label);
    if (p) {
      reply << p->toJSON() << '\n';
    } else {
      reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}\n";
    }
  } else if (word == "list") {
    reply << "{";
    int i = Pollable::pollables.size();
    for (PollableSet::iterator ips = Pollable::pollables.begin(); ips != Pollable::pollables.end(); ++ips, --i) {
      reply << "\"" << ips->second->label << "\":" << ips->second->toJSON() << (i > 1 ? "," : "");
    }
    reply << "}\n";
  } else if (word == "start" || word == "stop") {
    string label;
    cmd >> label;
    bool doStop = word == "stop";
    Pollable *p = Pollable::lookupByName(label);
    if (p) {
      if (doStop)
        p->stop(realTimeNow);
      else
        p->start(realTimeNow);
      reply << p->toJSON() << '\n';
      Pollable::requestPollFDRegen();
    } else {
      reply << "{\"error\": \"Error: '" << label << "' does not specify a known open device\"}\n";
    }
  } else if (word.substr(0, 3) == "raw") {
    string label;
    cmd >> label;
    unsigned rate = 0;
    cmd >> rate;
    uint32_t frames = 0;
    cmd >> frames; // this is @ frames for rawFile, FM demod flag for rawStream
    char path_template [MAX_CMD_STRING_LENGTH + 1];
    path_template[0] = 0;
    cmd.ignore(MAX_CMD_STRING_LENGTH, '"');
    cmd.getline(path_template, MAX_CMD_STRING_LENGTH, '"');

    DevMinder *p = dynamic_cast < DevMinder * > (Pollable::lookupByName(label));
    if (p) {
      if (word == "rawStream") {
        // set fm on/off and add a raw listener
        // cancelling the listen will close the connection.
        p->setDemodFMForRaw(frames);
        p->addRawListener(connLabel, round(p->hwRate / rate), true);
      } else if (word == "rawStreamOff") {
        p->removeRawListener(connLabel);
      } else if (word == "rawFile" || word == "rawFileOff") {
        std::string wavLabel = label + "_FileWriter";
        if (word == "rawFile") {
          if (strlen(path_template) == 0) {
            reply << "{\"error\": \"Error: invalid path template - did you forget double quotes?\"}\n";
          } else {
            WavFileWriter * wav = dynamic_cast < WavFileWriter * > (Pollable::lookupByName(wavLabel));
            // if there is already a raw listener on that device, just change
            // its path template so it can begin recording another file
            if (wav) {
              wav->resumeWithNewFile(path_template);
            } else {
              new WavFileWriter (label, wavLabel, path_template, frames, rate, p->numChan);
              p->addRawListener(wavLabel, round(p->hwRate / rate));
            }
          }
        } else {
          p->removeRawListener(wavLabel);
          Pollable::remove(wavLabel);
        }
        reply << "{}\n";
      }
    } else {
      reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}\n";
    }
  } else if (word == "fmOn" || word == "fmOff") {
    string label;
    cmd >> label;
    DevMinder *p = dynamic_cast < DevMinder * > (Pollable::lookupByName(label));
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
      DevMinder * ptr = DevMinder::getDevMinder(alsaDev, rate, numChan, label, realTimeNow);
      reply << ptr->toJSON() << '\n';
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "close") {
    string label;
    cmd >> label;
    DevMinder *dev = dynamic_cast < DevMinder * > (Pollable::lookupByName(label));
    if (dev) {
      dev->stop(realTimeNow);
      reply << dev->toJSON() << '\n';
      Pollable::remove(label);
    } else {
      reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}\n";
    }
    Pollable::requestPollFDRegen();
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
      DevMinder *dev = dynamic_cast < DevMinder * > (Pollable::lookupByName(devLabel));
      if (!dev)
        throw std::runtime_error(string("There is no device with label '") + devLabel + "'");
      if (Pollable::lookupByName(pluginLabel))
        throw std::runtime_error(string("There is already a device or plugin with label '") + pluginLabel + "'");
      new PluginRunner(pluginLabel, devLabel, dev->rate, dev->hwRate, dev->numChan, pluginLib, pluginName, outputName, ps);
      shared_ptr < PluginRunner > plugin = static_pointer_cast < PluginRunner > (Pollable::lookupByNameShared(pluginLabel));
      dev->addPluginRunner(pluginLabel, plugin);
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
      PollableSet::iterator ip = Pollable::pollables.find(pluginLabel);
      if (ip == Pollable::pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      shared_ptr < PluginRunner > p = boost::dynamic_pointer_cast < PluginRunner > (ip->second);
      PluginRunner * ptr = p.get();
      if (ptr)
        ptr->setParameters(ps);
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "detach") {
    string pluginLabel;
    cmd >> pluginLabel;
    try {
      PollableSet::iterator ip = Pollable::pollables.find(pluginLabel);
      if (ip == Pollable::pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      Pollable::pollables.erase(ip);
      reply << "{\"message\": \"Plugin " << pluginLabel << " has been detached.\"}\n";
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "receive") {
    string pluginLabel;
    cmd >> pluginLabel;
    try {
      PollableSet::iterator ip = Pollable::pollables.find(pluginLabel);
      if (ip == Pollable::pollables.end())
        throw std::runtime_error(string("There is no attached plugin with label '") + pluginLabel + "'");
      shared_ptr < PluginRunner > p = boost::dynamic_pointer_cast < PluginRunner > (ip->second);
      PluginRunner * ptr = p.get();
      if (ptr)
        ptr->addOutputListener(connLabel);
    } catch (std::runtime_error e) {
      reply << "{\"error\": \"Error:" << e.what() << "\"}\n";
    };
  } else if (word == "receiveAll") {
    for (PollableSet::iterator ip = Pollable::pollables.begin(); ip != Pollable::pollables.end(); ++ip) {
      shared_ptr < PluginRunner > p = boost::dynamic_pointer_cast < PluginRunner > (ip->second);
      PluginRunner * ptr = p.get();
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
    rv = Pollable::poll(2000); // 2 second timeout
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

          "       rawStream DEV_LABEL RATE FRAMES\n"
          "          Write raw data to the TCP connection.\n"
          "          DEV_LABEL: the device from which to obtain raw data\n"
          "          RATE:   the frame rate to use.  The actual frame rate will be the closest frame rate which\n"
          "                  divides evenly into the hardware frame rate.\n"
          "          FRAMES: the number of frames to write.  After the last frame is written, VAH will print a\n"
          "                  message of the form {\"message\": \"rawDone\", \"dev\": \"DEV_LABEL\"} to the TCP connection\n"
          "                  which issued the rawFile command.\n"
          "          If an error occurs when writing to a file, VAH will print a message of the form\n"
          "                  {\"message\": \"rawError\", \"dev\": \"DEV_LABEL\", \"errno\": errno} to the TCP connection\n"

          "       rawFile DEV_LABEL RATE FRAMES PATH_TEMPLATE\n"
          "          Write queued raw data to a file or the TCP connection.\n"
          "          DEV_LABEL: the device from which to obtain raw data; nothing is written until a rawOn\n"
          "                  command has been issued for this device.\n"
          "          RATE:   the frame rate to use.  The actual frame rate will be the closest frame rate which\n"
          "                  divides evenly into the hardware frame rate.\n"
          "          FRAMES: the number of frames to write.  After the last frame is written, VAH will print a\n"
          "                  message of the form {\"message\": \"rawDone\", \"dev\": \"DEV_LABEL\"} to the TCP connection\n"
          "                  which issued the rawFile command.\nRaw data will continue to be buffered so that a\n"
          "                  subsequent rawFile command can direct data to a new file without dropping frames.\n"
          "                  If a file is already being written from DEV_LABEL, this command closes that file.\n"
          "                  and immediately begins writing to the new file.\n"
          "          PATH_TEMPLATE: the template for a full pathname of the file to write; strftime format codes\n"
          "                  will be replaced by the real timestamp of the first frame written.\n"
          "                  If not specified, data will be written directly to the TCP connection.\n\n"
          "          If an error occurs when writing to a file, VAH will print a message of the form\n"
          "                  {\"message\": \"rawError\", \"dev\": \"DEV_LABEL\", \"errno\": errno} to the TCP connection\n"

          "       rawStreamOff DEV_LABEL\n"
          "          Stop writing raw data from the device DEV_LABEL to the issuing TCP connection.\n"
          "          Note: this command does not return a reply unless there is an error.\n\n"

          "       rawFileOff DEV_LABEL\n"
          "          Stop writing raw data from the device DEV_LABEL to a file, and stop queuing raw data.\n"

          "       fmOn DEV_LABEL\n"
          "          Specify that raw data from the device DEV_LABEL will be FM-demodulated\n"
          "          before being sent to to any file or TCP connection which are listening to it via\n"
          "          a rawFile command.  This does not change the data seen by plugins.\n"
          "          Also, this command does not by itself cause raw data to be emitted; you\n"
          "          must use rawOn command on a connection for this command to have any effect.\n"
          "          Finally, this command only has effect on a stereo device, and will reduce\n"
          "          the raw output to a single channel.  This command is likely only to be\n"
          "          useful for radio receivers which output I/Q as stereo audio channels.\n\n"

          "       fmOff DEV_LABEL\n"
          "          Turn off FM-demodulation for the specified device;  raw data will be sent\n"
          "          to any listening TCP connections as-is.\n"

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

std::string VampAlsaHost::defaultOutputListener;
