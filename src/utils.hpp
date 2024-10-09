#pragma once

#include "precomp.hpp"

struct Output
{
  Output() { }

 /**
 * @brief Currently supported are: ":highgui" and video files
 * 
 * @param s Filename or output name
 * @param imgSize Image size to write
 * @return Output object
 */
  static std::shared_ptr<Output> create(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) = 0;

  virtual ~Output() { }
};

//TODO: this
struct Source
{
  Source() { }

  /**
   * @brief Currently supported are: ":bars" and image files
   * 
   * @param s Filename or source name
   * @return Source object
   */
  static std::shared_ptr<Source> create(const std::string& s);

  virtual cv::Size getImageSize() = 0;

  virtual ~Source() {}
};

struct Log
{
    // TODO: variadic
    static void write(int level, const std::string& s);
    static void setVerbosity(int n);
    static int  getVerbosity();
    static void setProgName(const std::string& s);
};


cv::Mat loadImage(const std::string& fname);

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