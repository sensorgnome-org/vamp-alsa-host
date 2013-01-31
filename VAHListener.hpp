#ifndef VAHLISTENER_HPP
#define VAHLISTENER_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#include "Pollable.hpp"

/* a class for listening on a port for connections */

class VAHListener : public Pollable {

 protected:
  struct pollfd pollfd;
  struct sockaddr_in serv_addr;
  int SO_REUSEADDR_ON;
  int server_port_num;

 public:

  int getNumPollFDs() { return 1;};
                      // return number of fds used by this Pollable (negative means error)
  int getPollFDs (struct pollfd * pollfds);

  void handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow);

  VAHListener(int server_port_num, PollableMinder * minder);
};

#endif // VAHLISTENER_HPP
