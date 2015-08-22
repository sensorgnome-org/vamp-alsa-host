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
#include <memory>
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

#include "Pollable.hpp"
#include "VampAlsaHost.hpp"
#include "TCPListener.hpp"
#include "TCPConnection.hpp"

class PluginRunner;
class AlsaMinder;

#include "PluginRunner.hpp"
#include "AlsaMinder.hpp"

static VampAlsaHost *host;
  
#define HOST_VERSION "1.4"

static string serverSocketName = "/tmp/VAH.sock";            // port on which we listen for connections

static const string appname="vamp-alsa-host";

void
getFeaturesToBuffer(int, Plugin::FeatureSet, string, long long * totalFeatures);

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

        "Usage:\n" << name << " [-q] [-s SOCKNAME] &\n"
        "    -- Runs a server which listens and replies to commands via\n"
        "       unix domain socket SOCKNAME, which is created in /tmp\n"
        "       SOCKNAME defaults to " << serverSocketName << endl << 
        "       Reply text is terminated by an empty line.\n\n"

        "    Specifying '-q' tells the server not to print the welcome message to clients.\n\n"

        "    The server accepts the following commands on SOCKNAME:\n\n"
         << VampAlsaHost::commandHelp;
}

void terminate (int p) 
{
    
    if (Pollable::terminating)
        return;
    Pollable::terminating = true;
    delete host;
    std::cerr << "vamp-alsa-host terminating with code " << p << std::endl;
    std::cerr.flush();
    exit(p);
}

int 
main(int argc, char **argv)
{
    enum {
        COMMAND_HELP = 'h',
        COMMAND_SOCKET_NAME = 's',
        COMMAND_QUIET = 'q'

    };

    int option_index;
    static const char short_options[] = "hs:q";
    static const struct option long_options[] = {
        {"help", 0, 0, COMMAND_HELP},
        {"socket", 1, 0, COMMAND_SOCKET_NAME},
        {"quiet", 0, 0, COMMAND_QUIET},
        {0, 0, 0, 0}
    };

    int c;
    bool quiet = false;

    while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (c) {
        case COMMAND_HELP:
            usage(appname);
            exit(0);
        case COMMAND_SOCKET_NAME:
            {
                serverSocketName = string(optarg);
                if (serverSocketName.find('/') != string::npos) {
                    std::cerr << "error: socket name must not contain '/': it is always created in /tmp\n";
                    std::cerr.flush();
                    exit(2);
                }
                serverSocketName = string("/tmp/") + optarg;
            }
            break;
        case COMMAND_QUIET:
            quiet = true;
            break;
        default:
            usage(appname);
            exit(1);
        }
    }

    // remove existing socket from filespace, with safeguards

    struct stat sock_info;
    bool okay = true;
    if (stat(serverSocketName.c_str(), & sock_info)) {
        if (errno != ENOENT)
            okay = false;
    } else {
        if ( ! S_ISSOCK(sock_info.st_mode))
            okay = false;
    }
    if (!okay) {
        std::cerr << "error: '" << serverSocketName << "' exists and has wrong permissions or is not a socket\n";
        std::cerr.flush();
        exit(3);
    };
    unlink(serverSocketName.c_str());

    // handle signals gracefully

    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);
    signal(SIGSEGV, terminate);
    signal(SIGILL, terminate);
    signal(SIGFPE, terminate);
    signal(SIGABRT, terminate);

    ostringstream label("Socket:", ios_base::app);
    label << serverSocketName;
    host = new VampAlsaHost();
    new TCPListener(serverSocketName, label.str(), quiet);
    int rv = 0;
    try {
        rv = host->run();
    } catch (std::runtime_error e) {
        cerr << "vamp-alsa-host terminated\nWhy: " << e.what();
    };
    exit(rv);
}

