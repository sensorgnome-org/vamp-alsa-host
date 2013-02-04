#include "TCPListener.hpp"
#include "TCPConnection.hpp"
#include <stdexcept>

int TCPListener::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

void TCPListener::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {
  // accept a connection
  if (pollfds->revents & (POLLIN | POLLPRI)) {
    struct sockaddr_in cli_addr;
    memset( (char *) &cli_addr, 0, sizeof(cli_addr));
    socklen_t clilen = sizeof(cli_addr);
    int conn_fd = accept4(pollfd.fd, (struct sockaddr *) &cli_addr, &clilen, SOCK_NONBLOCK);
    if (conn_fd >= 0) {
      newConnectionHandler(conn_fd);
    }
  }
};

TCPListener::TCPListener(int server_port_num, NewConnectionHandler newConnectionHandler) : 
  Pollable(0),
  SO_REUSEADDR_ON(1),
  server_port_num(server_port_num),
  newConnectionHandler(newConnectionHandler)
{
    
  pollfd.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (pollfd.fd < 0)
    throw std::runtime_error("Error opening socket for TCPListener");

  setsockopt(pollfd.fd, SOL_SOCKET, SO_REUSEADDR, &SO_REUSEADDR_ON, sizeof(SO_REUSEADDR_ON));
  pollfd.events = POLLIN | POLLPRI;

  memset( (char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(0x7f000001); // localhost
  serv_addr.sin_port = htons(server_port_num);
  if (bind(pollfd.fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    throw std::runtime_error(string("Error binding server to port\n"));
  }
  if (listen(pollfd.fd, 5))
    throw std::runtime_error(string("Error listening on port\n"));
};
