/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Vamp-Alsa-Host - manage a number of ALSA audio devices, run
    a VAMP plugin on each, and send output to stdout.  Control is
    via a TCP socket.

    Copyright 2012 John Brzustowski
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    vamp-alsa-host is based on these programs:

    1. vamp-simple-host, which is part of

        VAMP
        An API for audio analysis and feature extraction plugins.

        Centre for Digital Music, Queen Mary, University of London.
        Copyright 2006 Chris Cannam, copyright 2007-2008 QMUL.

        Permission is hereby granted, free of charge, to any person
        obtaining a copy of this software and associated documentation
        files (the "Software"), to deal in the Software without
        restriction, including without limitation the rights to use, copy,
        modify, merge, publish, distribute, sublicense, and/or sell copies
        of the Software, and to permit persons to whom the Software is
        furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice shall be
        included in all copies or substantial portions of the Software.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
        EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
        MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
        NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
        ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
        CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
        WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the names of the Centre for
        Digital Music; Queen Mary, University of London; and Chris Cannam
        shall not be used in advertising or otherwise to promote the sale,
        use or other dealings in this Software without prior written
        authorization.

    2. aplay, which is part of the alsautils package:

            aplay.c
            Copyright (c) by Jaroslav Kysela <perex@perex.cz>
            *  Based on vplay program by Michael Beck

            License: GPL V2 or later
    */

/*
   vamp-alsa-host was originally intended to read from one or more
   funcubedongle radio receivers, process the signal through a VAMP
   plugin, and write the features found by the plugin to one or more
   socket 

   The funcubedongle streams baseband I/Q output as stereo USB audio
   at 96ksps in S16_LE format.  Due to apparent quirks in the
   funcubedongle and/or the USB implementation on the beaglebone
   computer where this code was first deployed, vamp-alsa-host
   provides the ability to stop and restart each device independently,
   so that device settings such as VHF frequency can be changed.  These
   settings are sent to a USB HID interface on the funcubedongle, and
   attempting to do so while the device is streaming audio is
   unreliable, at least on the beaglebone + USB hub platform.

   Each line of output is prepended with the label for its
   funcubedongle.  Audio data is read using alsa and poll(), and we
   try to provide real timestamps.  On the bonedongle (i.e. beaglebone
   + funcubedongle), timestamp precision appears to be around 1ms or
   less.
   
   FIXME:
   Sample format is hard-coded as S16_LE to match the funcubedongle.

 */

#include <vamp-hostsdk/PluginHostAdapter.h>
#include <vamp-hostsdk/PluginInputDomainAdapter.h>
#include <vamp-hostsdk/PluginBufferingAdapter.h>
#include <vamp-hostsdk/PluginLoader.h>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <set>
#include <time.h>

#include <cstring>
#include <cstdlib>
#include <vector>
#include <alsa/asoundlib.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>

//#include "system.h"
#include "error.h"
//#include "libfcd.h"

#include <cmath>
#include <stdexcept>
#include <endian.h>
#include <signal.h>
#include <inttypes.h>

using namespace std;

using Vamp::Plugin;
using Vamp::PluginHostAdapter;
using Vamp::RealTime;
using Vamp::HostExt::PluginLoader;
using Vamp::HostExt::PluginWrapper;
using Vamp::HostExt::PluginInputDomainAdapter;


#include "ParamSet.hpp"

static int serverPortNum = 0xbd09;            // port on which we listen for TCP connections

static const string appname="vamp-alsa-host";
static const string commandHelp =         
    "       open LABEL AUDIO_DEV RATE NUM_CHANNELS\n"
    "          Opens an audio device so that plugins can be attached to it.\n"
    "          To start processing, you must attach a plugin and start the device 'start LABEL' command - see below\n"
    "          Arguments:\n"
    "          LABEL: a label which will identify this audio device in subsequent commands\n"
    "             and in output lines\n"
    "          AUDIO_DEV: the ALSA name of the audio device (e.g. 'default:CARD=V10')\n"
    "          RATE: the sampling rate to use for the device (e.g. 48000)\n"
    "          NUM_CHANNELS: the number of channels to read from the device (usually 1 or 2)\n\n"
    "          e.g. open 3 default:CARD=V10_2 48000 2\n\n"

    "       attach DEV_LABEL PLUGIN_LABEL PLUGIN_SONAME PLUGIN_ID PLUGIN_OUTPUT [PAR VALUE]*\n"
    "          DEV_LABEL: the label for the input device, which must already have been opened with open\n"
    "          PLUGIN_LABEL: the label for this plugin instance, for use in subsequent commands\n"
    "          PLUGIN_SONAME: the name (without path) of the library containing the plugin\n"
    "          PLUGIN_ID: the name of the plugin within the library\n"
    "          PLUGIN_OUTPUT: the name of the desired output from the plugin\n"
    "                         (some plugins have multiple outputs - you must pick one)\n"
    "          [PAR VALUE]: an optional set of plugin parameter settings, where:\n"
    "                       PAR: is the name of a plugin parameter\n"
    "                       VALUE: is the value to be assiged to the parameter\n\n"
    "          e.g. attach 3 pulse3 lotek-plugins.so findpulsefdbatch pulses minsnr=6\n\n"

    "       start LABEL\n"
    "          Begin acquiring data from the audio device identified by LABEL.\n"
    "          This must already have been created using an 'open' command.\n"
    "          Data are read from device LABEL, and passed to any attached plugins.\n"
    "          Output from a plugin instance is sent to the socket from which the\n"
    "          corresponding 'start' command was issued.\n\n"

    "       stop LABEL\n"
    "          Stop acquiring data from the audio device identified by LABEL.\n\n"

    "Note: start and stop commands can be sent repeatedly for a device which has been opened.\n"
    "The associated plugin will see a continuous stream of data, albeit with timestamps\n"
    "reflecting the loss of data between stop and (re)start.  The same applies to startAll and stopAll.\n"
    "To re-start a plugin completely, you must close the device and re-open it.\n\n"

    "       close LABEL\n"
    "           Stop acquiring data from the device and shut it down.  This deletes\n"
    "           the device from the server, so that LABEL cannot be used in subsequent commands\n"
    "           until another 'open LABEL...' command is sent.\n\n"

    "       status LABEL\n"
    "           Report on the status of the audio device or plugin identified by LABEL\n"
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

void
getFeaturesToBuffer(int, Plugin::FeatureSet, string, long long * totalFeatures);

double
now(bool is_monotonic = true) {
    struct timespec clockTime;
    clock_gettime(is_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME, &clockTime);
    return clockTime.tv_sec + clockTime.tv_nsec / (double) 1.0e9;
};

#include "Pollable.hpp"
#include "PollableMinder.hpp"
#include "VAHListener.hpp"
#include "VAHConnection.hpp"

static PluginLoader *pluginLoader = 0;

class PluginRunner;
class AlsaMinder;

typedef std::map < string, AlsaMinder *> AlsaMinderNamedSet;
typedef std::map < string, PluginRunner *> PluginRunnerNamedSet;
typedef std::set < VAHConnection *> VAHConnectionSet;

#include "PluginRunner.hpp"
#include "AlsaMinder.hpp"

// this is the owner of all pollabel objects:  AlsaMinders, VAHConnections, and VAHListeners
static PollableMinder minder;

static AlsaMinderNamedSet alsas;         // does not own objects pointed to by members
static PluginRunnerNamedSet plugins;


#define HOST_VERSION "1.4"

void 
run(PollableMinder & minder);

void 
usage(string name) {
    cerr << endl << name << ": A command-line host for running Vamp audio analysis plugins\n"
        "on data from one or more USB audio devices.\n\n"

        "Copyright 2012 John Brzustowski.\n\n"

        "Includes code from the VAMP plugin SDK which is:\n"
        "Copyright 2006-2009 Chris Cannam and QMUL.\n"
        "Centre for Digital Music, Queen Mary, University of London.\n\n"
        "Also based on the aplay utility from the alsautils package,\n"
        "which is licensed under GNU GPL V2.0\n"
         << name << " is freely redistributable under GNU GPL V2.0 or later\n\n"

        "Usage:\n" << name << " [-p PORTNO]&\n"
        "    -- Runs a server which listens and replies to commands via TCP/IP on port PORTNO\n"
        "       PORTNO defaults to " << serverPortNum << endl << 
        "       Reply text is terminated by an empty line.\n\n"

        "    The server accepts the following commands on its TCP/IP port:\n\n"
         << commandHelp;
}



void 
terminate (int p) 
{
    static bool terminating = false;
    
    if (terminating)
        return;
    terminating = true;

    for (AlsaMinderNamedSet::iterator fcdi = alsas.begin(); fcdi != alsas.end(); ++fcdi) {
        AlsaMinder *fcd = fcdi->second;
        delete fcd;
        fcdi->second = 0;
    }

    
    cerr << appname << " terminated with signal or code " << p << endl;

    exit(p);
}




void run(PollableMinder & minder)
{
    cout << setprecision(3);

    int rv;
    do {
        rv = minder.poll(10000);
    } while (rv == 0);
    terminate(rv);
}


string runCommand(string cmdString, VAHConnection *conn) {
    ostringstream reply;
    string word;
    istringstream cmd(cmdString);
    double timeNow = now();
    if (! (cmd >> word))
        return reply.str();
    if (word == "stopAll") {
        // quick stop of all devices
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 50 * 1000 * 1000;
        for (AlsaMinderNamedSet::iterator fcdi = alsas.begin(); fcdi != alsas.end(); ++fcdi) {
            fcdi->second->requestStop(timeNow);
            // sleep 50 ms between stops
            nanosleep(&t, 0);
        }
        nanosleep (&t, 0);
        reply << "{\"message\":\"All devices stopped.\"}";
        minder.requestPollFDRegen();
    } else if (word == "startAll") {
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 500 * 1000 * 1000;
        for (AlsaMinderNamedSet::iterator fcdi = alsas.begin(); fcdi != alsas.end(); ++fcdi) {
            fcdi->second->requestStart(timeNow);
            // sleep 500 ms between starts
            nanosleep(&t, 0);
        }
        reply << "{\"message\":\"All devices started.\"}";
        minder.requestPollFDRegen();
    } else if (word == "status") {
        string label;
        cmd >> label;
        AlsaMinderNamedSet::iterator fcdi = alsas.find(label);
        if (fcdi != alsas.end()) {
            AlsaMinder *fcd = fcdi->second;
            reply << fcd->toJSON();
        } else {
            reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}";
        }
    } else if (word == "list") {
        reply << "{";
        int i = alsas.size();
        for (AlsaMinderNamedSet::iterator fcdi = alsas.begin(); fcdi != alsas.end(); ++fcdi, --i) {
            AlsaMinder *fcd = fcdi->second;
            reply << "\"" << fcd->label << "\":" << fcd->toJSON() << (i > 1 ? "," : "");
        }
        reply << "}";
    } else if (word == "start" || word == "stop") {
        string label;
        cmd >> label;
        bool doStop = word == "stop";
        AlsaMinderNamedSet::iterator fcdi = alsas.find(label);
        if (fcdi != alsas.end()) {
            AlsaMinder *fcd = fcdi->second;
            if (doStop)
                fcd->requestStop(timeNow);
            else
                fcd->requestStart(timeNow);
            reply << fcd->toJSON();
            minder.requestPollFDRegen();
        } else {
            reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}";
        }
    } else if (word == "open" ) {
        string label, alsaDev;
        int rate, numChan;
        cmd >> label >> alsaDev >> rate >> numChan;
        try {
            minder.add(alsas[label] = new AlsaMinder(alsaDev, rate, numChan, label, timeNow));
            minder.requestPollFDRegen();
            reply << alsas[label]->toJSON();
        } catch (std::runtime_error e) {
            reply << "{\"error\": \"Error:" << e.what() << "\"}";
        };
    } else if (word == "close") {
        string label;
        cmd >> label;
        AlsaMinderNamedSet::iterator fcdi = alsas.find(label);
        if (fcdi != alsas.end()) {
            AlsaMinder *fcd = fcdi->second;
            fcd->requestStop(timeNow);
            reply << fcd->toJSON();
            delete fcd;
            alsas.erase(fcdi);
        } else {
            reply << "{\"error\": \"Error: LABEL does not specify a known open device\"}";
        }
        minder.requestPollFDRegen();
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
            AlsaMinderNamedSet::iterator fcdi = alsas.find(devLabel);
            if (fcdi == alsas.end())
                throw std::runtime_error(string("There is no device with label '") + devLabel + "'");
            PluginRunner *plugin = new PluginRunner(pluginLabel, devLabel, fcdi->second->rate, fcdi->second->numChan, pluginLib, pluginName, outputName, ps, conn);
            plugins[pluginLabel] = plugin;
            fcdi->second->addPluginRunner(plugin);
            reply << plugin->toJSON();
        } catch (std::runtime_error e) {
            reply << "{\"error\": \"Error:" << e.what() << "\"}";
        };
    } else if (word == "quit" ) {
        reply << "{\"message\": \"Terminating server.\"}";
        terminate(0);
    } else if (word == "help" ) {
        reply <<  "Commands:\n" << commandHelp << endl; // NB: don't use JSON for this
    } else {
        reply << "{\"error\": \"Error: invalid command\"}";
    }
    reply << endl;
    return reply.str();
};

int 
main(int argc, char **argv)
{
    enum {
        COMMAND_HELP = 'h',
        COMMAND_PORT_NUM = 'p'
    };

    int option_index;
    static const char short_options[] = "hp:";
    static const struct option long_options[] = {
        {"help", 0, 0, COMMAND_HELP},
        {"port", 1, 0, COMMAND_PORT_NUM},
        {0, 0, 0, 0}
    };

    int c;

    while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (c) {
        case COMMAND_HELP:
            usage(appname);
            exit(0);
        case COMMAND_PORT_NUM:
            serverPortNum = atoi(optarg);
            break;
        default:
            usage(appname);
            exit(1);
        }
    }

    // handle signals gracefully

    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);
    signal(SIGSEGV, terminate);
    signal(SIGILL, terminate);
    signal(SIGFPE, terminate);
    signal(SIGABRT, terminate);

    VAHConnection::setCommandHandler(& runCommand);

    minder.add (new VAHListener(serverPortNum));
    run(minder);
}

