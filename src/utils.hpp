#pragma once

#include "precomp.hpp"

namespace atv
{

struct Log
{
    // TODO: variadic
    static void write(int level, const std::string& s);
    static void setVerbosity(int n);
    static int  getVerbosity();
    static void setProgName(const std::string& s);
};


cv::Mat loadImage(const std::string& fname);

std::optional<int> parseInt(const std::string &s);

// splits string by a delimiting char, start and end of source string:
// ":asdf:qwer:" by ":" -> ["", "asdf", "qwer", ""]
// "asdf:qwer"   by ":" -> ["asdf", "qwer"]
std::vector<std::string> split(const std::string& s, char d);

struct CmdArgument
{
  enum class Type
  {
    BOOL, INT, LIST_INT, STRING, LIST_STRING
  };

  Type type;
  bool optional;
  std::string exampleArgs;
  std::vector<std::string> help;

  CmdArgument(const std::string& _exampleArgs, const Type& _type,
              bool _optional, const std::string& _help) :
    type(_type),
    optional(_optional),
    exampleArgs(_exampleArgs),
    help()
  {
    size_t p0 = 0;
    for (size_t i = 0; i < _help.length(); i++)
    {
      if (_help.at(i) == '\n')
      {
        help.push_back(_help.substr(p0, i - p0));
        p0 = i+1;
      }
    }
    if (p0 < _help.length())
    {
      help.push_back(_help.substr(p0, _help.length() - p0 - 1));
    }
  }
};

typedef std::variant<bool, int, std::string, std::vector<int>, std::vector<std::string>> ArgType;
std::map<std::string, ArgType> parseCmdArgs(const std::map<std::string, CmdArgument>& knownArgs, int nArgs, char** argv);

void showUsage(const std::string& message, const std::string& appName, const std::map<std::string, CmdArgument>& knownArgs);

} // ::atv
