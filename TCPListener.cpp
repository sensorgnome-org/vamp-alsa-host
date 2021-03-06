#include "TCPListener.hpp"
#include "TCPConnection.hpp"
#include <stdexcept>
#include <fcntl.h>

string TCPListener::toJSON() {
  ostringstream s;
  s << "{" 
    << "\"type\":\"TCPListener\","
    << "\"socket\":\"" << server_socket_name << "\""
    << "}";
  return s.str();
}

int TCPListener::getNumPollFDs() {
  return 1;
};

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
      new TCPConnection (conn_fd, label.str(), & VampAlsaHost::runCommand, quiet, timeNow);
      if (! Pollable::haveControlSocket())
        Pollable::setControlSocket(label.str());
    }
  }
};

TCPListener::TCPListener(string server_socket_name, string label, bool quiet) : 
  Pollable(label),
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

