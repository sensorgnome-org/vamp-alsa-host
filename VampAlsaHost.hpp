#ifndef VAMPALSAHOST_HPP
#define VAMPALSAHOST_HPP

/*
  Manage a set of pollable objects, do polling, and call the object when events occur.

*/

#include <set>
#include <vector>
#include <map>
#include <poll.h>
#include <errno.h>
#include <string>
#include <sstream>
#include <memory>

using std::string;
using std::istringstream;
using std::ostringstream;

// type that handles commands
typedef string (*CommandHandler) (string cmd, string connLabel);

class VampAlsaHost {

protected:
  static string defaultOutputListener; // label of connection which will be automatically added as an outputListener to any new attached plugin 

public:
  static const unsigned MAX_CMD_STRING_LENGTH = 512;    // size of buffer for receiving commands over TCP

  VampAlsaHost();
  ~VampAlsaHost();
  static string runCommand(string cmdString, string connLabel);
  int run();
  static double now(bool is_monotonic = false);
  static const string commandHelp;

protected:
};

#endif // VAMPALSAHOST_HPP
