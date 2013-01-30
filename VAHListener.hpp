#ifndef VAHLISTENER_HPP
#define VAHLISTENER_HPP

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
  int getPollFDs (struct pollfd * pollfds) {
    * pollfds = fd;
  };

  void handleEvent (struct pollfd *pollfds, bool timedOut, &regenFDs, PollableMinder *minder) {
    // accept a connection
    if (pollfds->revents & (POLLIN | POLLPRI)) {
      struct sockaddr_in cli_addr;
      memset( (char *) &cli_addr, 0, sizeof(cli_addr));
      socklen_t clilen = sizeof(cli_addr);
      int conn_fd = accept4(listenerPollfd.fd, (struct sockaddr *) &cli_addr, &clilen, SOCK_NONBLOCK);
      if (conn_fd >= 0) {
        minder->add(new VAHConnection(conn_fd));
      }
    }
  };

  VAHListener(int server_port_num) : 
    server_port_num(server_port_num),
    SO_REUSEADDR_ON(1) 
  {
    
    pollfd.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (pollfd.fd < 0)
      throw std::runtime_error("Error opening socket for VAHListener");

    setsockopt(pollfd.fd, SOL_SOCKET, SO_REUSEADDR, &SO_REUSEADDR_ON, sizeof(SO_REUSEADDR_ON));
    pollfd.events = POLLIN | POLLPRI;

    memset( (char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(0x7f000001); // localhost
    serv_addr.sin_port = htons(server_port_num);
    if (bind(pollfd.fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
      throw std::runtime_error(string("Error (") + errno + ") binding server on port " + server_port_num + "\n");
    }
    if (listen(pollfd.fd, 5))
      throw std::runtime_error(string("Error (") + errno + ") listening on port " + server_port_num + "\n");
  };
}

#endif // VAHLISTENER_HPP
