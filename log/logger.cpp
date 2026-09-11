#include <cmw/log/logger.h>
#include <cmw/log/logstream.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdexcept>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>

namespace hnu    {
namespace cmw   {
namespace logger {


namespace {

string LogPath(const string& filename)
{
    // Callers supply a name, not an output directory. This also handles argv[0].
    const string::size_type slash = filename.find_last_of("/\\");
    string name = filename.substr(slash == string::npos ? 0 : slash + 1);
    if (name.empty() || name == "." || name == "..") {
        throw std::logic_error("invalid log file name: " + filename);
    }
    if (name.size() < 4 || name.substr(name.size() - 4) != ".log") {
        name += ".log";
    }

    const char* root = getenv("CMW_PATH");
#ifdef CMW_PROJECT_ROOT
    if (root == nullptr || *root == '\0') {
        root = CMW_PROJECT_ROOT;
    }
#endif
    char resolved_root[PATH_MAX];
    if (root == nullptr || *root == '\0' || realpath(root, resolved_root) == nullptr) {
        throw std::logic_error("cannot locate project root for logs; set CMW_PATH");
    }
    const string directory = string(resolved_root) + "/log";
    if (mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
        throw std::logic_error("create log directory failed: " + directory);
    }
    struct stat status;
    if (lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
        throw std::logic_error("log directory must be a directory: " + directory);
    }
    const string path = directory + "/" + name;
    // Do not follow a pre-existing symlink out of the log directory.
    if (lstat(path.c_str(), &status) == 0) {
        if (!S_ISREG(status.st_mode)) {
            throw std::logic_error("log file must be a regular file: " + path);
        }
    } else if (errno != ENOENT) {
        throw std::logic_error("inspect log file failed: " + path);
    }
    return path;
}

}  // namespace

const char* Logger::s_level[LOG_COUNT] =
{
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};


Logger::Logger() : m_max(0), m_len(0), m_level(LOG_DEBUG), m_console(true)
{
}

Logger::~Logger()
{
    close();
}



void Logger::open(const string &filename)
{
    const string path = LogPath(filename);
    if (m_fout.is_open()) {
        close();
    }
    m_fout.clear();
    m_filename = path;
    m_fout.open(path, std::ios::app);
    if (m_fout.fail())
    {
        throw std::logic_error("open log file failed: " + path);
    }
    m_fout.seekp(0, std::ios::end); //移动文件写指针到文件的末尾
    m_len = m_fout.tellp();     //返回当前写指针在文件中的位置，即从文件开始到写指针当前位置的字节数
}

void Logger::close()
{
    m_fout.close();
}

void Logger::log(Level level, const char* file, int line, const char* format, ...)
{
    if (m_level > level)
    {
        return;
    }

    if (m_fout.fail())
    {
        return;
    }

    ostringstream oss;
    time_t ticks = time(NULL);
    struct tm* ptm = localtime(&ticks);
    char timestamp[32];
    memset(timestamp, 0, sizeof(timestamp));
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", ptm);

    int len = 0;
    //计算格式化所需缓冲区大小
    const char * fmt = "%s %s %s:%d ";
    len = snprintf(NULL, 0, fmt, timestamp, s_level[level], file, line);
    if (len > 0)
    {
        char * buffer = new char[len + 1];
        //格式化字符串
        snprintf(buffer, len + 1, fmt, timestamp, s_level[level], file, line);
        buffer[len] = 0;
        oss << buffer;
        delete [] buffer;
        m_len += len;
    }


    va_list arg_ptr;
    va_start(arg_ptr, format);
    //计算可变参数的长度
    len = vsnprintf(NULL, 0, format, arg_ptr);
    va_end(arg_ptr);
    if (len > 0)
    {
        char * content = new char[len + 1];
        va_start(arg_ptr, format);
        //格式化可变参数
        vsnprintf(content, len + 1, format, arg_ptr);
        va_end(arg_ptr);
        content[len] = 0;
        oss << content;
        delete [] content;
        m_len += len;
    }

    oss << "\n";
    const string & str = oss.str();
    if (m_console)
    {
        std::cout << str << std::endl;
    }
    m_fout << str;
    //确保数据立即写入到文件中，而不是保存在缓存中
    m_fout.flush();

    if (m_max > 0 && m_len >= m_max)
    {
        rotate();
    }
}

//限制文件大小
void Logger::max(int bytes)
{
    m_max = bytes;
}

void Logger::level(int level)
{
    m_level = level;
}

void Logger::console(bool console)
{
    m_console = console;
}

//如果超出文件大小则新创建一个文件
void Logger::rotate()
{
    close();
    time_t ticks = time(NULL);
    struct tm* ptm = localtime(&ticks);
    char timestamp[32];
    memset(timestamp, 0, sizeof(timestamp));
    strftime(timestamp, sizeof(timestamp), ".%Y-%m-%d_%H-%M-%S", ptm);
    string filename = m_filename + timestamp;
    if (rename(m_filename.c_str(), filename.c_str()) != 0)
    {
        throw std::logic_error("rename log file failed: " + string(strerror(errno)));
    }
    open(m_filename);
}

LogStream Logger::logStream(Level level,const char* file, int line){
    return LogStream(level, file, line);
}

}
}
}