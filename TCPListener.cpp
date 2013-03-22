#include "TCPListener.hpp"
#include "TCPConnection.hpp"
#include <stdexcept>
#include <fcntl.h>

int TCPListener::getPollFDs (struct pollfd * pollfds) {
  * pollfds = pollfd;
  return 0;
};

void TCPListener::handleEvents (struct pollfd *pollfds, bool timedOut, double timeNow) {
  // accept a connection
  if (pollfds->revents & (POLLIN | POLLPRI)) {
    struct sockaddr_un cli_addr;
    memset( (char *) &cli_addr, 0, sizeof(cli_addr));
    socklen_t clilen = sizeof(cli_addr);
    int conn_fd = accept(pollfd.fd, (struct sockaddr *) &cli_addr, &clilen);
    if (conn_fd >= 0) {
      fcntl(conn_fd, F_SETFL, O_NONBLOCK);
      ostringstream label("Socket#", std::ios_base::app);
      label << conn_fd;
      auto conn = std::make_shared < TCPConnection > (conn_fd, host, label.str(), quiet);
      host->add(conn);
    }
  }
};

TCPListener::TCPListener(string server_socket_name, string label, VampAlsaHost *host, bool quiet) : 
  Pollable(host, label),
  server_socket_name(server_socket_name),
  quiet(quiet)
{
  pollfd.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (pollfd.fd < 0)
    throw std::runtime_error("Error opening socket for TCPListener");

  pollfd.events = POLLIN | POLLPRI;

  memset( (char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strncpy(serv_addr.sun_path, server_socket_name.c_str(), sizeof(serv_addr.sun_path) - 1);
  if (bind(pollfd.fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    throw std::runtime_error(string("Error binding server to port\n"));
  }
  if (listen(pollfd.fd, 5))
    throw std::runtime_error(string("Error listening on port\n"));
  std::cout << "Listening on " << server_socket_name << std::endl;
  std::cout.flush();
};

void TCPListener::stop(double timeNow) {
  /* do nothing */
};

int TCPListener::start(double timeNow) {
  return 0;
};

string TCPListener::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"type\":\"TCPListener\","
    << "\"socket\":\"" << server_socket_name << "\""
    << "}";
  return s.str();
}
