CCOPTS_PRODUCTION :=  -std=c++0x -I. -O3 -Wall -fPIC -ftree-vectorize -ffast-math  

CCOPTS_DEBUG :=  -std=c++0x -I. -g3 -Wall -fPIC -ftree-vectorize -ffast-math

CCOPTS := $(CCOPTS_PRODUCTION)
#CCOPTS := $(CCOPTS_DEBUG)

all: vamp-alsa-host

clean:
	rm -f *.o vamp-alsa-host

install: vamp-alsa-host
	strip vamp-alsa-host
	cp vamp-alsa-host /usr/bin
	cp vamp-host /usr/bin

AlsaMinder.o: AlsaMinder.cpp
	g++ $(CCOPTS) -c -o $@ $<

PluginRunner.o: PluginRunner.cpp
	g++ $(CCOPTS) -c -o $@ $<

Pollable.o: Pollable.cpp
	g++ $(CCOPTS) -c -o $@ $<

VampAlsaHost.o: VampAlsaHost.cpp
	g++ $(CCOPTS) -c -o $@ $<

TCPListener.o: TCPListener.cpp
	g++ $(CCOPTS) -c -o $@ $<

TCPConnection.o: TCPConnection.cpp
	g++ $(CCOPTS) -c -o $@ $<

WavFileWriter.o: WavFileWriter.cpp
	g++ $(CCOPTS) -c -o $@ $<

vamp-alsa-host.o: vamp-alsa-host.cpp
	g++ $(CCOPTS) -c -o $@ $<

vamp-host.o: vamp-host.cpp
	g++  $(CCOPTS) -c -o $@ $<

vamp-alsa-host:  vamp-alsa-host.o TCPListener.o TCPConnection.o Pollable.o PluginRunner.o VampAlsaHost.o AlsaMinder.o WavFileWriter.o
	g++ $(CCOPTS) -o $@ $^ -lasound -lm -ldl -lrt -lvamp-hostsdk

vamp-host: vamp-host.o
	g++  -o $@ $^ -lvamp-hostsdk -lsndfile -ldl

# DO NOT DELETE THIS LINE -- make depend depends on it.

AlsaMinder.o: AlsaMinder.hpp Pollable.hpp VampAlsaHost.hpp PluginRunner.hpp
AlsaMinder.o: ParamSet.hpp
OutputListener.o: OutputListener.hpp
PluginRunner.o: PluginRunner.hpp ParamSet.hpp Pollable.hpp VampAlsaHost.hpp
PluginRunner.o: AlsaMinder.hpp
TCPConnection.o: TCPConnection.hpp Pollable.hpp VampAlsaHost.hpp
TCPListener.o: TCPListener.hpp Pollable.hpp VampAlsaHost.hpp
TCPListener.o: TCPConnection.hpp
VampAlsaHost.o: VampAlsaHost.hpp Pollable.hpp AlsaMinder.hpp PluginRunner.hpp
VampAlsaHost.o: ParamSet.hpp WavFileWriter.hpp
vamp-alsa-host.o: ParamSet.hpp Pollable.hpp VampAlsaHost.hpp TCPListener.hpp
vamp-alsa-host.o: TCPConnection.hpp PluginRunner.hpp AlsaMinder.hpp
vamp-host.o: system.h
WavFileWriter.o: WavFileWriter.hpp Pollable.hpp VampAlsaHost.hpp
AlsaMinder.o: Pollable.hpp VampAlsaHost.hpp PluginRunner.hpp ParamSet.hpp
AlsaMinder.o: AlsaMinder.hpp
PluginRunner.o: ParamSet.hpp Pollable.hpp VampAlsaHost.hpp AlsaMinder.hpp
PluginRunner.o: PluginRunner.hpp
Pollable.o: VampAlsaHost.hpp
TCPConnection.o: Pollable.hpp VampAlsaHost.hpp
TCPListener.o: Pollable.hpp VampAlsaHost.hpp
WavFileWriter.o: Pollable.hpp VampAlsaHost.hpp
