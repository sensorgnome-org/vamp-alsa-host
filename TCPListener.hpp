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
  struct pollfd pollfd;
  struct sockaddr_un serv_addr;
  string server_socket_name;
  bool quiet;

 public:

  int getNumPollFDs() { return 1;};
                      // return number of fds used by this Pollable (negative means error)
  int getPollFDs (struct pollfd * pollfds);

  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  TCPListener(string server_socket_name, string label, VampAlsaHost *host, bool quiet);

  void stop(double timeNow);

  int start(double timeNow);

  string toJSON();

};
#endif // TCPLISTENER_HPP
