#include "precomp.hpp"
#include "utils.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace atv
{

std::vector<std::string> split(const std::string& s, char d)
{
    std::vector<std::string> tokens;
    size_t pos = 0;
    for (size_t i = 0; i < s.length(); i++)
    {
        if (s.at(i) == d)
        {
            tokens.push_back(s.substr(pos, i - pos));
            pos = i + 1;
        }
    }
    tokens.push_back(s.substr(pos, s.length() - pos));

    return tokens;
}

// I/O

cv::Mat loadImage(const std::string& fname)
{
  assert(!fname.empty());

  cv::Mat img = cv::imread(fname, cv::IMREAD_UNCHANGED);

  if (img.empty())
  {
    std::cerr << "Failed to load image " << fname << std::endl;
    abort();
  }

  if (img.depth() != CV_8U)
  {
    std::cerr << "Image depth is not 8 bit: " << fname << std::endl;
    abort();
  }

  cv::Mat cvt4;
  if (img.channels() == 1)
  {
    cv::cvtColor(img, cvt4, cv::COLOR_GRAY2BGRA);
  }
  else if (img.channels() == 3)
  {
    //TODO: BGR to RGB?
    cv::cvtColor(img, cvt4, cv::COLOR_BGR2BGRA);
  }
  else if (img.channels() == 4)
  {
    //TODO: BGR to RGB?
    cvt4 = img;
  }
  else
  {
    std::cerr << "Unknown format for file " << fname << std::endl;
    abort();
  }

  Log::write(2, "loaded " + fname + " " + std::to_string(img.cols) + "x" + std::to_string(img.rows));

  return cvt4;
}


// Command line parser

std::optional<int> parseInt(const std::string &s)
{
    int v;
    size_t at;
    try
    {
        v = std::stoi(s, &at);
    }
    catch (std::invalid_argument const &ex)
    {
        std::cerr << "Failed to parse \"" << s << "\": " << ex.what() << std::endl;
        return {};
    }
    catch (std::out_of_range const &ex)
    {
        std::cerr << "Failed to parse \"" << s << "\": " << ex.what() << std::endl;
        return {};
    }

    if (at != s.length())
    {
        std::cerr << "Failed to parse \"" << s << "\": only " << at << " symbols parsed" << std::endl;
        return {};
    }

    return v;
}

bool isArgName(const std::string &arg)
{
    return arg.length() > 2 && arg[0] == '-' && arg[1] == '-';
}

typedef std::variant<bool, int, std::string, std::vector<int>, std::vector<std::string>> ArgType;
std::map<std::string, ArgType> parseCmdArgs(const std::map<std::string, CmdArgument> &knownArgs, int nArgs, char **argv)
{
    std::map<std::string, ArgType> usedArgs;

    std::set<std::string> mandatoryArgsToFill;
    for (const auto &[k, v] : knownArgs)
    {
        if (!v.optional)
        {
            mandatoryArgsToFill.insert(k);
        }
    }

    for (int i = 1; i < nArgs; i++)
    {
        std::string arg(argv[i]);
        if (!isArgName(arg))
        {
            std::cerr << "Argument starting from \"--\" expected, instead we got " << arg << std::endl;
            return {};
        }

        std::string name = arg.substr(2, arg.length() - 2);
        if (knownArgs.count(name) != 1)
        {
            std::cerr << "Argument \"" << name << "\" is not known" << std::endl;
            return {};
        }

        if (usedArgs.count(name) > 0)
        {
            std::cerr << "Argument \"" << name << "\" was already used" << std::endl;
            return {};
        }

        ArgType value;
        switch (knownArgs.at(name).type)
        {
        case CmdArgument::Type::BOOL:
            value = true;
            break;
        case CmdArgument::Type::INT:
        {
            i++;
            if (i >= nArgs)
            {
                std::cerr << "Argument \"" << name << "\" requires int argument" << std::endl;
                return {};
            }
            auto v = parseInt(argv[i]);
            if (v)
            {
                value = v.value();
            }
            else
            {
                return {};
            }
            break;
        }
        case CmdArgument::Type::STRING:
        {
            i++;
            if (i >= nArgs)
            {
                std::cerr << "Argument \"" << name << "\" requires string argument" << std::endl;
                return {};
            }
            value = std::string(argv[i]);
            break;
        }
        case CmdArgument::Type::LIST_INT:
        {
            std::vector<int> listInt;
            while (++i < nArgs)
            {
                if (isArgName(argv[i]))
                {
                    i--;
                    break;
                }
                auto v = parseInt(argv[i]);
                if (v)
                {
                    listInt.push_back(v.value());
                }
                else
                {
                    return {};
                }
            }

            if (listInt.empty())
            {
                std::cerr << "Argument \"" << name << "\" requires a list of integers" << std::endl;
                return {};
            }
            value = listInt;
            break;
        }
        case CmdArgument::Type::LIST_STRING:
        {
            std::vector<std::string> listStr;
            while (++i < nArgs)
            {
                if (isArgName(argv[i]))
                {
                    i--;
                    break;
                }
                listStr.push_back(argv[i]);
            }

            if (listStr.empty())
            {
                std::cerr << "Argument \"" << name << "\" requires a list of strings" << std::endl;
                return {};
            }
            value = listStr;
            break;
        }
        default:
            break;
        }
        usedArgs[name] = value;
    }

    for (const auto &[name, val] : usedArgs)
    {
        if (mandatoryArgsToFill.count(name))
        {
            mandatoryArgsToFill.erase(name);
        }
    }

    if (!mandatoryArgsToFill.empty())
    {
        std::cerr << "Following args are required:";
        for (const auto &k : mandatoryArgsToFill)
        {
            std::cerr << " " << k;
        }
        std::cerr << std::endl;
        return {};
    }

    return usedArgs;
}


void showUsage(const std::string& message, const std::string& appName, const std::map<std::string, CmdArgument>& knownArgs)
{
    std::cout << message << std::endl;

    std::cout << "Usage: " << appName;
    for (const auto &[k, v] : knownArgs)
    {
        if (!v.optional)
        {
            std::cout << " --" << k << " " << v.exampleArgs;
        }
    }
    std::cout << " [other keys are optional]" << std::endl;

    std::cout << "Keys:" << std::endl;
    for (const auto &[k, v] : knownArgs)
    {
        std::cout << "    --" << k << std::string(12 - k.length(), ' ');
        std::cout << v.exampleArgs << std::endl;
        for (const auto &hs : v.help)
        {
            std::cout << "      " << hs << std::endl;
        }
    }
}

// Logging 

static std::string progname = "";
static int verbose_p = 0;

void Log::setVerbosity(int n)
{
    verbose_p = n;
}

int Log::getVerbosity()
{
    return verbose_p;
}

void Log::setProgName(const std::string& s)
{
    progname = s;
}

void Log::write(int level, const std::string& s)
{
    if (verbose_p >= level)
    {
        std::cerr << progname << ": " << s << std::endl;
    }
}

} // ::atv
