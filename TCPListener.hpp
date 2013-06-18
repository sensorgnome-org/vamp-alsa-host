#ifndef TCPLISTENER_HPP
#define TCPLISTENER_HPP

#include <sys/socket.h>
#include <sys/un.h>
//#include <netinet/in.h>
#include <string.h>
#include <iostream>
#include <sstream>

#include "Pollable.hpp"

/* a class for listening on a port for connections */

class TCPListener : public Pollable {

 protected:
  struct sockaddr_un serv_addr;
  string server_socket_name;
  bool quiet;

 public:

  string toJSON();

  bool queueOutput (const char * p, uint32_t len, void * meta = 0) {return true;};

  int getNumPollFDs() { return 1;};
                      // return number of fds used by this Pollable (negative means error)

  int getPollFDs (struct pollfd * pollfds);

  int getOutputFD() {return -1;}; // no output FD

  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  TCPListener(string server_socket_name, string label, bool quiet);

  void stop(double timeNow) {};

  int start(double timeNow) {return 0;};

};
#endif // TCPLISTENER_HPP
