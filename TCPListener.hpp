#ifndef TCPLISTENER_HPP
#define TCPLISTENER_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <iostream>
#include <sstream>

#include "Pollable.hpp"

/* a class for listening on a port for connections */

class TCPListener : public Pollable {

 protected:
  struct pollfd pollfd;
  struct sockaddr_in serv_addr;
  int SO_REUSEADDR_ON;
  int server_port_num;
  bool quiet;

 public:

  int getNumPollFDs() { return 1;};
                      // return number of fds used by this Pollable (negative means error)
  int getPollFDs (struct pollfd * pollfds);

  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  TCPListener(int server_port_num, string label, VampAlsaHost *host, bool quiet);

  void stop(double timeNow);

  int start(double timeNow);

  string toJSON();

};
#endif // TCPLISTENER_HPP
