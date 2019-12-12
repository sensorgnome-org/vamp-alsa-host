CCOPTS := -DRPI -I. -Wall -fPIC -ftree-vectorize -ffast-math -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard

CXX := g++

.PHONY: all clean debug install

all: vamp-alsa-host
all: CCOPTS += -g -O3
  
debug: vamp-alsa-host
debug: CCOPTS += -g3 -O

clean:
	rm -f *.o vamp-alsa-host

install: vamp-alsa-host
	strip vamp-alsa-host
	cp vamp-alsa-host /usr/bin

AlsaMinder.o: AlsaMinder.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

RTLSDRMinder.o: RTLSDRMinder.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

DevMinder.o: DevMinder.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

PluginRunner.o: PluginRunner.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

Pollable.o: Pollable.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

VampAlsaHost.o: VampAlsaHost.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

TCPListener.o: TCPListener.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

TCPConnection.o: TCPConnection.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

WavFileWriter.o: WavFileWriter.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

vamp-alsa-host.o: vamp-alsa-host.cpp
	$(CXX) $(CCOPTS) -c -o $@ $<

vamp-alsa-host:  vamp-alsa-host.o TCPListener.o TCPConnection.o Pollable.o PluginRunner.o VampAlsaHost.o AlsaMinder.o WavFileWriter.o DevMinder.o RTLSDRMinder.o
	$(CXX) $(CCOPTS) -o $@ $^ -lasound -lm -ldl -lrt -lvamp-hostsdk -lboost_filesystem -lboost_system -lboost_thread -lfftw3f -lpthread

# DO NOT DELETE THIS LINE -- make depend depends on it.

AlsaMinder.o: AlsaMinder.hpp Pollable.hpp VampAlsaHost.hpp PluginRunner.hpp DevMinder.hpp
AlsaMinder.o: ParamSet.hpp
DevMinder.o: DevMinder.hpp Pollable.hpp VampAlsaHost.hpp PluginRunner.hpp
DevMinder.o: ParamSet.hpp
PluginRunner.o: PluginRunner.hpp ParamSet.hpp Pollable.hpp VampAlsaHost.hpp
PluginRunner.o: AlsaMinder.hpp
TCPConnection.o: TCPConnection.hpp Pollable.hpp VampAlsaHost.hpp
TCPListener.o: TCPListener.hpp Pollable.hpp VampAlsaHost.hpp
TCPListener.o: TCPConnection.hpp
VampAlsaHost.o: VampAlsaHost.hpp Pollable.hpp AlsaMinder.hpp PluginRunner.hpp
VampAlsaHost.o: ParamSet.hpp WavFileWriter.hpp
vamp-alsa-host.o: ParamSet.hpp Pollable.hpp VampAlsaHost.hpp TCPListener.hpp
vamp-alsa-host.o: TCPConnection.hpp PluginRunner.hpp AlsaMinder.hpp
WavFileWriter.o: WavFileWriter.hpp Pollable.hpp VampAlsaHost.hpp
AlsaMinder.o: Pollable.hpp VampAlsaHost.hpp PluginRunner.hpp ParamSet.hpp
AlsaMinder.o: AlsaMinder.hpp
PluginRunner.o: ParamSet.hpp Pollable.hpp VampAlsaHost.hpp AlsaMinder.hpp
PluginRunner.o: PluginRunner.hpp
Pollable.o: VampAlsaHost.hpp
TCPConnection.o: Pollable.hpp VampAlsaHost.hpp
TCPListener.o: Pollable.hpp VampAlsaHost.hpp
WavFileWriter.o: Pollable.hpp VampAlsaHost.hpp
