CCOPTS_PRODUCTION :=  -std=c++0x -I. -O3 -Wall -fPIC -ftree-vectorize -ffast-math

CCOPTS_DEBUG :=  -std=c++0x -I. -g3 -Wall -fPIC -ftree-vectorize -ffast-math 

CCOPTS := $(CCOPTS_DEBUG)

all: vamp-alsa-host

clean:
	rm -f *.o vamp-alsa-host

AlsaMinder.o: AlsaMinder.cpp
	gcc $(CCOPTS) -c -o $@ $<

PluginRunner.o: PluginRunner.cpp
	gcc $(CCOPTS) -c -o $@ $<

PollableMinder.o: PollableMinder.cpp
	gcc $(CCOPTS) -c -o $@ $<

TCPListener.o: TCPListener.cpp
	gcc $(CCOPTS) -c -o $@ $<

TCPConnection.o: TCPConnection.cpp
	gcc $(CCOPTS) -c -o $@ $<

vamp-alsa-host.o: vamp-alsa-host.cpp
	gcc $(CCOPTS) -c -o $@ $<

vamp-alsa-host:  vamp-alsa-host.o TCPListener.o TCPConnection.o PluginRunner.o PollableMinder.o AlsaMinder.o
	gcc $(CCOPTS) -o $@ $^ -lasound -lm -ldl -lrt -lvamp-hostsdk

# DO NOT DELETE THIS LINE -- make depend depends on it.

AlsaMinder.o: AlsaMinder.hpp Pollable.hpp PollableMinder.hpp PluginRunner.hpp
AlsaMinder.o: ParamSet.hpp TCPConnection.hpp
PluginRunner.o: PluginRunner.hpp ParamSet.hpp TCPConnection.hpp Pollable.hpp
PluginRunner.o: PollableMinder.hpp AlsaMinder.hpp
PollableMinder.o: PollableMinder.hpp Pollable.hpp
TCPConnection.o: TCPConnection.hpp Pollable.hpp PollableMinder.hpp
TCPListener.o: TCPListener.hpp Pollable.hpp PollableMinder.hpp
TCPListener.o: TCPConnection.hpp
vamp-alsa-host.o: ParamSet.hpp Pollable.hpp PollableMinder.hpp
vamp-alsa-host.o: TCPListener.hpp TCPConnection.hpp PluginRunner.hpp
vamp-alsa-host.o: AlsaMinder.hpp
