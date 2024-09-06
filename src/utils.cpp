#include "precomp.hpp"
#include "utils.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

// I/O

cv::Mat loadImage(const std::string& fname)
{
  assert(!fname.empty());

  cv::Mat img = cv::imread(fname, cv::IMREAD_UNCHANGED);

  if (img.empty())
  {
    std::cout << "Failed to load image " << fname << std::endl;
    abort();
  }

  if (img.depth() != CV_8U)
  {
    std::cout << "Image depth is not 8 bit: " << fname << std::endl;
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
    std::cout << "Unknown format for file " << fname << std::endl;
    abort();
  }

    Log::write(2, "loaded " + fname + " " + std::to_string(img.cols) + "x" + std::to_string(img.rows));

  return cvt4;
}

// Output classes

struct HighguiOutput : Output
{
  HighguiOutput();

  void send(const cv::Mat& m) override;

  ~HighguiOutput();
};

struct VideoOutput : Output
{
  VideoOutput(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) override;

  ~VideoOutput() { }

  cv::VideoWriter writer;
};


HighguiOutput::HighguiOutput()
{
    cv::namedWindow("tv");
}

void HighguiOutput::send(const cv::Mat &m)
{
    cv::imshow("tv", m);
    cv::waitKey(1);
}

HighguiOutput::~HighguiOutput()
{
    cv::destroyAllWindows();
}

// used with ffmpeg:
// const enum AVCodecID video_codec = AV_CODEC_ID_H264;
// const enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;

VideoOutput::VideoOutput(const std::string &s, cv::Size imgSize)
{
    // cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
    if (!writer.open(s, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), /* fps */ 30, imgSize))
    {
        throw std::runtime_error("Failed to open VideoWriter");
    }
    Log::write(2, "opened " + s + " " + std::to_string(imgSize.width) + "x" + std::to_string(imgSize.height));
}

void VideoOutput::send(const cv::Mat &m)
{
    cv::Mat out = m;
    if (m.channels() == 4)
    {
        cvtColor(m, out, cv::COLOR_BGRA2BGR);
    }
    writer.write(out);
}

std::shared_ptr<Output> Output::create(const std::string &s, cv::Size imgSize)
{
    if (s.at(0) == ':')
    {
        std::string name = s.substr(1, s.length() - 1);
        if (name == "highgui")
        {
            return std::make_shared<HighguiOutput>();
        }
        else
        {
            throw std::runtime_error("Unknown video output: " + name);
        }
    }
    else
    {
        return std::make_shared<VideoOutput>(s, imgSize);
    }
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
        std::cout << "Failed to parse " << s << ": " << ex.what() << std::endl;
        return {};
    }
    catch (std::out_of_range const &ex)
    {
        std::cout << "Failed to parse " << s << ": " << ex.what() << std::endl;
        return {};
    }

    if (at != s.length())
    {
        std::cout << "Failed to parse " << s << ": only " << at << " symbols parsed" << std::endl;
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
            std::cout << "Argument starting from \"--\" expected, instead we got " << arg << std::endl;
            return {};
        }

        std::string name = arg.substr(2, arg.length() - 2);
        if (knownArgs.count(name) != 1)
        {
            std::cout << "Argument " << name << " is not known" << std::endl;
            return {};
        }

        if (usedArgs.count(name) > 0)
        {
            std::cout << "Argument " << name << " was already used" << std::endl;
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
                std::cout << "Argument " << name << "requires int argument" << std::endl;
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
                std::cout << "Argument " << name << "requires string argument" << std::endl;
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
                std::cout << "Argument " << name << " requires a list of integers" << std::endl;
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
                std::cout << "Argument " << name << " requires a list of strings" << std::endl;
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
        std::cout << "Following args are required:";
        for (const auto &k : mandatoryArgsToFill)
        {
            std::cout << " " << k;
        }
        std::cout << std::endl;
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

