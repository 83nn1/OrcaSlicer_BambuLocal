#include "LanFtpsTunnel.hpp"

#include "libslic3r/miniz_extension.hpp"

#include <curl/curl.h>
#include <miniz.h>
#include <openssl/md5.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include "nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace Slic3r {
namespace LanFtps {

using json = nlohmann::json;
namespace fs = boost::filesystem;

namespace {

// Request / result codes, must match PrinterFileSystem.
enum {
    LIST_INFO             = 0x0001,
    SUB_FILE              = 0x0002,
    FILE_DEL              = 0x0003,
    FILE_DOWNLOAD         = 0x0004,
    FILE_UPLOAD           = 0x0005,
    REQUEST_MEDIA_ABILITY = 0x0007,
    TASK_CANCEL           = 0x1000,
};

enum {
    SUCCESS               = 0,
    CONTINUE              = 1,
    ERROR_JSON            = 2,
    ERROR_PIPE            = 3,
    ERROR_CANCEL          = 4,
    FILE_NO_EXIST         = 10,
    FILE_NAME_INVALID     = 11,
    FILE_READ_WRITE_ERR   = 14,
    FILE_TYPE_ERR         = 16,
    API_VERSION_UNSUPPORT = 18,
    STORAGE_SPACE_NOT_ENOUGH = 20,
    FILE_CREATE_ERR       = 21,
    FILE_WRITE_ERR        = 22,
    MD5_COMPARE_ERR       = 23,
    SEND_ERR              = 25,
};

constexpr char const *SCHEME          = "lanftps://";
constexpr int         FTPS_PORT       = 990;
constexpr size_t      CHUNK_SIZE      = 256 * 1024;
constexpr size_t      MAX_QUEUED      = 32;
constexpr uintmax_t   CACHE_MAX_BYTES = 512ull * 1024 * 1024;
constexpr int         LIST_RESYNC_NOTIFY = 0x0101;
constexpr uint32_t    NO_REQUEST         = 0xFFFFFFFFu;
constexpr size_t      UPLOAD_CHUNK_KB    = 256;             // fragment size PrinterFileSystem sends
constexpr size_t      UPLOAD_QUEUE_MAX   = 4 * 1024 * 1024; // fragments waiting for the FTPS connection
constexpr int         UPLOAD_WAIT_SECS   = 30;              // no fragment for that long: give up

std::string url_decode(std::string const &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string url_encode(std::string const &s)
{
    static char const hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string file_name_of(std::string const &path)
{
    auto n = path.find_last_of('/');
    return n == std::string::npos ? path : path.substr(n + 1);
}

std::string dir_of(std::string const &path)
{
    auto n = path.find_last_of('/');
    return n == std::string::npos || n == 0 ? std::string("/") : path.substr(0, n);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(tolower(c)); });
    return s;
}

bool ends_with_ci(std::string const &s, std::string const &suffix)
{
    return s.size() >= suffix.size() && lower(s.substr(s.size() - suffix.size())) == suffix;
}

// days since 1970-01-01 for a civil date (proleptic Gregorian)
int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t make_utc(int year, int month, int day, int hour, int minute)
{
    return time_t(days_from_civil(year, unsigned(month), unsigned(day)) * 86400 + hour * 3600 + minute * 60);
}

struct Entry
{
    std::string name;
    std::string path;
    uint64_t    size = 0;
    time_t      time = 0;
    bool        dir  = false;
};

// Parses one line of a unix style LIST reply, e.g.
// "-rw-rw-rw-   1 root  root   1640240 Jun 18 00:27 video_2025-06-17_14-48-33.mp4"
// "-rw-rw-rw-   1 root  root   1640240 Jun 18  2024 Some name with spaces.3mf"
bool parse_list_line(std::string line, Entry &e)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    if (line.size() < 10)
        return false;
    std::istringstream iss(line);
    std::string perms, links, user, group, size, mon, day, time_or_year;
    if (!(iss >> perms >> links >> user >> group >> size >> mon >> day >> time_or_year))
        return false;
    std::string name;
    std::getline(iss, name);
    size_t p = name.find_first_not_of(' ');
    if (p == std::string::npos)
        return false;
    name = name.substr(p);
    if (name == "." || name == "..")
        return false;

    static char const *months[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    int month = 0;
    for (int i = 0; i < 12; ++i)
        if (lower(mon) == months[i]) month = i + 1;
    if (month == 0)
        return false;

    int d = atoi(day.c_str());
    int year = 0, hour = 0, minute = 0;
    time_t now = ::time(nullptr);
    if (time_or_year.find(':') != std::string::npos) {
        sscanf(time_or_year.c_str(), "%d:%d", &hour, &minute);
        // no year given: the printer does that for files of the last 12 months,
        // so it is the most recent such date that is not in the future
        std::tm tm_now{};
#ifdef _WIN32
        gmtime_s(&tm_now, &now);
#else
        gmtime_r(&now, &tm_now);
#endif
        year = tm_now.tm_year + 1900;
        if (make_utc(year, month, d, hour, minute) - now > 2 * 86400) // tolerate clock / time zone offsets
            --year;
    } else {
        year = atoi(time_or_year.c_str());
    }

    e.name = name;
    e.dir  = perms[0] == 'd';
    e.size = std::strtoull(size.c_str(), nullptr, 10);
    e.time = make_utc(year, month, d, hour, minute);
    return true;
}

// The printer has no reliable clock in LAN only mode (it restarts from an old
// date after every power cycle), so the modification time of the files on the
// storage is useless for sorting. Every entry of a 3mf written by OrcaSlicer or
// Bambu Studio carries the local time of the PC at export (slicing) instead,
// starting with the local file header at offset 0 of the file.
constexpr size_t ZIP_HEADER_SIZE = 30;

time_t zip_entry_time(unsigned char const *p, size_t size)
{
    if (size < ZIP_HEADER_SIZE || p[0] != 'P' || p[1] != 'K' || p[2] != 3 || p[3] != 4)
        return 0;
    unsigned dos_time = p[10] | (p[11] << 8);
    unsigned dos_date = p[12] | (p[13] << 8);
    std::tm  tm{};
    tm.tm_year  = int(dos_date >> 9) + 80;
    tm.tm_mon   = int((dos_date >> 5) & 15) - 1;
    tm.tm_mday  = int(dos_date & 31);
    tm.tm_hour  = int(dos_time >> 11);
    tm.tm_min   = int((dos_time >> 5) & 63);
    tm.tm_sec   = int(dos_time & 31) * 2;
    tm.tm_isdst = -1;
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday == 0 || tm.tm_year < 110)
        return 0;
    time_t t = mktime(&tm);
    // the zip time is local time of the slicing PC, assume ours
    return t > 0 && t < ::time(nullptr) + 2 * 86400 ? t : 0;
}

std::string md5_hex(MD5_CTX &ctx)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    static char const hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : digest) {
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}

// Printers that are printing. Reading previews means downloading whole model
// files, which keeps a P1 / A1 so busy while it prints that it stops answering
// MQTT for minutes. The GUI marks them here, see set_printer_busy().
std::mutex            g_busy_mutex;
std::set<std::string> g_busy_hosts;

bool printer_busy(std::string const &host)
{
    std::lock_guard<std::mutex> l(g_busy_mutex);
    return g_busy_hosts.count(host) > 0;
}

class Tunnel
{
public:
    explicit Tunnel(std::string const &url)
    {
        std::string rest = std::string(url).substr(strlen(SCHEME));
        auto        q    = rest.find('?');
        m_host           = rest.substr(0, q);
        while (!m_host.empty() && m_host.back() == '/')
            m_host.pop_back();
        if (q != std::string::npos) {
            std::istringstream iss(rest.substr(q + 1));
            std::string        kv;
            while (std::getline(iss, kv, '&')) {
                auto e = kv.find('=');
                if (e == std::string::npos) continue;
                auto k = kv.substr(0, e), v = url_decode(kv.substr(e + 1));
                if (k == "user") m_user = v;
                else if (k == "passwd") m_passwd = v;
            }
        }
        if (m_user.empty()) m_user = "bblp";
    }

    ~Tunnel()
    {
        stop();
        if (m_curl) curl_easy_cleanup(m_curl);
    }

    bool valid() const { return !m_host.empty() && !m_passwd.empty(); }

    int open()
    {
        m_curl = curl_easy_init();
        if (!m_curl) return -1;
        // check login and connectivity once
        prepare(base_url() + "/");
        curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1L);
        CURLcode res = curl_easy_perform(m_curl);
        if (res != CURLE_OK) {
            BOOST_LOG_TRIVIAL(warning) << "LanFtps: connect to " << m_host << " failed: " << curl_easy_strerror(res);
            return ERROR_PIPE;
        }
        BOOST_LOG_TRIVIAL(info) << "LanFtps: connected to " << m_host;
        load_job_times();
        m_worker = std::thread([this] { run(); });
        return 0;
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_stopping = true;
        }
        m_cond.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }

    int send_message(char const *data, int len)
    {
        std::string msg(data, len);
        std::string param; // optional data after the request: json of a "mem:/" download, bytes of an upload
        auto        n = msg.find("\n\n");
        if (n != std::string::npos) {
            param = msg.substr(n + 2);
            msg.resize(n);
        }
        json root;
        try {
            root = json::parse(msg);
            if (root.value("cmdtype", 0) == FILE_UPLOAD && root["req"].contains("frag_id"))
                return upload_fragment(root.value("sequence", 0u), root["req"], std::move(param));
            if (!param.empty()) root["param"] = json::parse(param);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "LanFtps: invalid request";
            return 0;
        }
        int      type = root.value("cmdtype", 0);
        uint32_t seq  = root.value("sequence", 0u);
        if (type == TASK_CANCEL) {
            json tasks = json::array();
            {
                std::lock_guard<std::mutex> l(m_mutex);
                for (auto &t : root["req"]["tasks"]) {
                    m_cancelled.insert(t.get<uint32_t>());
                    tasks.push_back(t);
                }
            }
            m_cond.notify_all(); // a running upload waits for fragments
            json reply;
            reply["tasks"] = tasks;
            push(seq, SUCCESS, reply, nullptr, 0, false);
            return 0;
        }
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_requests.push_back(std::move(root));
        }
        m_cond.notify_all();
        return 0;
    }

    int read_sample(Bambu_Sample *sample)
    {
        std::unique_lock<std::mutex> l(m_mutex);
        if (m_responses.empty()) return Bambu_would_block;
        m_current = std::move(m_responses.front());
        m_responses.pop_front();
        l.unlock();
        m_cond.notify_all();
        sample->itrack      = 0;
        sample->size        = int(m_current.size());
        sample->flags       = 0;
        sample->buffer      = reinterpret_cast<unsigned char const *>(m_current.data());
        sample->decode_time = 0;
        return Bambu_success;
    }

private:
    // ---------------------------------------------------------------- queue

    void run()
    {
        while (true) {
            json req;
            {
                std::unique_lock<std::mutex> l(m_mutex);
                // requests first; deferred previews and missing job times only while
                // idle and the printer is not printing (polled, busy has no notification)
                m_cond.wait_for(l, std::chrono::seconds(1), [this] { return m_stopping || !m_requests.empty(); });
                if (m_stopping) return;
                if (m_requests.empty()) {
                    if (printer_busy(m_host)) continue;
                    if (!m_deferred.empty()) {
                        BOOST_LOG_TRIVIAL(info) << "LanFtps: printer idle, loading deferred previews";
                        for (auto &r : m_deferred)
                            if (m_cancelled.count(r.value("sequence", 0u)) == 0) m_requests.push_back(std::move(r));
                        m_deferred.clear();
                        continue;
                    }
                    if (m_pending_times.empty()) continue;
                    l.unlock();
                    fetch_next_job_time();
                    continue;
                }
                req = std::move(m_requests.front());
                m_requests.pop_front();
            }
            int      type = req.value("cmdtype", 0);
            uint32_t seq  = req.value("sequence", 0u);
            if (type == SUB_FILE && printer_busy(m_host) && !all_cached(req["req"])) {
                // previews wait until the print is over, PrinterFileSystem waits for the answer
                BOOST_LOG_TRIVIAL(info) << "LanFtps: printer is printing, preview deferred";
                std::lock_guard<std::mutex> l(m_mutex);
                if (m_cancelled.count(seq) == 0) m_deferred.push_back(std::move(req));
                continue;
            }
            m_seq         = seq;
            try {
                if (type == FILE_DOWNLOAD && req["req"].value("path", "").rfind("mem:/", 0) == 0)
                    project_file_download(seq, req.value("param", json::object()));
                else
                    handle(type, seq, req["req"]);
            } catch (std::exception const &e) {
                BOOST_LOG_TRIVIAL(warning) << "LanFtps: request " << type << " failed: " << e.what();
                push(seq, ERROR_JSON, json::object());
            }
        }
    }

    bool cancelled() const
    {
        std::lock_guard<std::mutex> l(m_mutex);
        return m_stopping || m_cancelled.count(m_seq) > 0;
    }

    // Queues one response: "<json>\n\n<binary data>"
    void push(uint32_t seq, int result, json const &reply, char const *data = nullptr, size_t size = 0, bool throttle = true)
    {
        json root;
        root["result"]   = result;
        root["sequence"] = seq;
        root["reply"]    = reply;
        // file names that are not valid UTF-8 must not break the whole reply
        std::string s = root.dump(-1, ' ', false, json::error_handler_t::replace);
        s += "\n\n";
        if (size) s.append(data, size);
        std::unique_lock<std::mutex> l(m_mutex);
        if (throttle)
            m_cond.wait(l, [this] { return m_stopping || m_responses.size() < MAX_QUEUED; });
        m_responses.push_back(std::move(s));
    }

    // Unsolicited message, handled by PrinterFileSystem's notify handlers
    void push_notify(int type, json const &notify)
    {
        json root;
        root["cmdtype"]  = type;
        root["sequence"] = 0;
        root["notify"]   = notify;
        std::string s    = root.dump(-1, ' ', false, json::error_handler_t::replace);
        s += "\n\n";
        std::lock_guard<std::mutex> l(m_mutex);
        m_responses.push_back(std::move(s));
    }

    void handle(int type, uint32_t seq, json const &req)
    {
        switch (type) {
        case LIST_INFO: list_info(seq, req); break;
        case SUB_FILE: sub_file(seq, req); break;
        case FILE_DOWNLOAD: file_download(seq, req); break;
        case FILE_DEL: file_delete(seq, req); break;
        case FILE_UPLOAD: file_upload(seq, req); break;
        case REQUEST_MEDIA_ABILITY: {
            json reply;
            reply["storage"] = json::array({"sdcard"});
            push(seq, SUCCESS, reply);
            break;
        }
        default:
            BOOST_LOG_TRIVIAL(info) << "LanFtps: unsupported request " << type;
            push(seq, API_VERSION_UNSUPPORT, json::object());
            break;
        }
    }

    // ---------------------------------------------------------------- FTPS

    std::string base_url() const { return "ftps://" + m_host + ":" + std::to_string(FTPS_PORT); }

    std::string remote_url(std::string const &path) const
    {
        // absolute paths on the printer, like other tools use them ("RETR /cache/x.3mf")
        std::string url = base_url() + "/";
        std::istringstream iss(path);
        std::string        seg;
        while (std::getline(iss, seg, '/')) {
            if (seg.empty()) continue;
            url += "/" + url_encode(seg);
        }
        if (!path.empty() && path.back() == '/') url += "/";
        return url;
    }

    static int progress_cb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return static_cast<Tunnel *>(clientp)->cancelled() ? 1 : 0;
    }

    void prepare(std::string const &url)
    {
        curl_easy_reset(m_curl);
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_USERNAME, m_user.c_str());
        curl_easy_setopt(m_curl, CURLOPT_PASSWORD, m_passwd.c_str());
        curl_easy_setopt(m_curl, CURLOPT_USE_SSL, (long) CURLUSESSL_ALL);
        // the printer uses a self-signed certificate
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(m_curl, CURLOPT_FTP_USE_EPSV, 0L);
        curl_easy_setopt(m_curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);
        curl_easy_setopt(m_curl, CURLOPT_FTP_FILEMETHOD, (long) CURLFTPMETHOD_NOCWD);
        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(m_curl, CURLOPT_XFERINFOFUNCTION, &Tunnel::progress_cb);
        curl_easy_setopt(m_curl, CURLOPT_XFERINFODATA, this);
    }

    struct Sink
    {
        std::function<bool(char const *, size_t)> fn;
        uint64_t                                 received = 0;
    };

    static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto  *sink = static_cast<Sink *>(userdata);
        size_t n    = size * nmemb;
        sink->received += n;
        return sink->fn(ptr, n) ? n : 0;
    }

    // Some printers close the TLS data connection without close_notify;
    // accept that if everything has arrived.
    static bool transfer_ok(CURLcode res, uint64_t received, uint64_t expected)
    {
        if (res == CURLE_OK) return true;
        if ((res == CURLE_RECV_ERROR || res == CURLE_PARTIAL_FILE || res == CURLE_SSL_SHUTDOWN_FAILED) && expected > 0 &&
            received == expected)
            return true;
        return false;
    }

    int map_error(CURLcode res) const
    {
        if (cancelled()) return ERROR_CANCEL;
        switch (res) {
        // connection level problems
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_LOGIN_DENIED:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_USE_SSL_FAILED:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_WEIRD_SERVER_REPLY:
        case CURLE_FTP_WEIRD_PASV_REPLY:
        case CURLE_FTP_CANT_GET_HOST:
        case CURLE_FTP_ACCEPT_FAILED:
        case CURLE_FTP_ACCEPT_TIMEOUT:
        case CURLE_PARTIAL_FILE: return ERROR_PIPE;
        case CURLE_WRITE_ERROR: return FILE_READ_WRITE_ERR;
        // the server answered, but refused (e.g. 550 file not found)
        default: return FILE_NO_EXIST;
        }
    }

    // LIST <dir>; returns 0 on success
    int list_dir(std::string const &dir, std::vector<Entry> &entries)
    {
        std::string data;
        Sink        sink{[&data](char const *p, size_t n) {
            data.append(p, n);
            return true;
        }};
        prepare(remote_url(dir.back() == '/' ? dir : dir + "/"));
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &Tunnel::write_cb);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &sink);
        CURLcode res = curl_easy_perform(m_curl);
        if (res != CURLE_OK && !(res == CURLE_RECV_ERROR && !data.empty() && data.back() == '\n')) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: LIST " << dir << " failed: " << curl_easy_strerror(res);
            return map_error(res);
        }
        std::istringstream iss(data);
        std::string        line;
        while (std::getline(iss, line)) {
            Entry e;
            if (!parse_list_line(line, e)) continue;
            e.path = (dir == "/" ? std::string() : dir) + "/" + e.name;
            entries.push_back(e);
        }
        return 0;
    }

    int remote_size(std::string const &path, uint64_t &size)
    {
        auto it = m_known.find(path);
        if (it != m_known.end()) {
            size = it->second.size;
            return 0;
        }
        prepare(remote_url(path));
        curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1L);
        CURLcode res = curl_easy_perform(m_curl);
        if (res != CURLE_OK) return map_error(res);
        curl_off_t len = -1;
        curl_easy_getinfo(m_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
        size = len < 0 ? 0 : uint64_t(len);
        return 0;
    }

    int download(std::string const &path, uint64_t expected, std::function<bool(char const *, size_t)> const &fn, uint64_t &received)
    {
        Sink sink{fn};
        prepare(remote_url(path));
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &Tunnel::write_cb);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &sink);
        CURLcode res = curl_easy_perform(m_curl);
        received     = sink.received;
        if (!transfer_ok(res, received, expected)) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: RETR " << path << " failed: " << curl_easy_strerror(res);
            return map_error(res);
        }
        return 0;
    }

    int download_to_string(std::string const &path, std::string &data)
    {
        uint64_t received = 0;
        return download(path, 0, [&data](char const *p, size_t n) { data.append(p, n); return true; }, received);
    }

    int remove(std::string const &path)
    {
        prepare(base_url() + "/");
        curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1L);
        std::string        cmd   = "DELE " + path;
        struct curl_slist *quote = curl_slist_append(nullptr, cmd.c_str());
        curl_easy_setopt(m_curl, CURLOPT_QUOTE, quote);
        CURLcode res = curl_easy_perform(m_curl);
        curl_slist_free_all(quote);
        if (res != CURLE_OK) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: DELE " << path << " failed: " << curl_easy_strerror(res);
            return map_error(res);
        }
        m_known.erase(path);
        return 0;
    }

    // ---------------------------------------------------------------- cache of model files

    fs::path cache_dir() const
    {
        boost::system::error_code ec;
        fs::path dir = fs::temp_directory_path(ec) / "orca_lan_storage";
        fs::create_directories(dir, ec);
        return dir;
    }

    fs::path cache_path(std::string const &path)
    {
        auto it = m_known.find(path);
        std::string key = m_host + "|" + path;
        if (it != m_known.end())
            key += "|" + std::to_string(it->second.size) + "|" + std::to_string(it->second.time);
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, key.data(), key.size());
        return cache_dir() / (md5_hex(ctx) + ".3mf");
    }

    void prune_cache()
    {
        boost::system::error_code ec;
        std::vector<std::pair<std::time_t, fs::path>> files;
        uintmax_t                                     total = 0;
        for (fs::directory_iterator it(cache_dir(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!fs::is_regular_file(it->path(), ec)) continue;
            total += fs::file_size(it->path(), ec);
            files.emplace_back(fs::last_write_time(it->path(), ec), it->path());
        }
        std::sort(files.begin(), files.end());
        for (auto &f : files) {
            if (total <= CACHE_MAX_BYTES) break;
            total -= (std::min<uintmax_t>)(total, fs::file_size(f.second, ec));
            fs::remove(f.second, ec);
        }
    }

    // Is the model file available locally? local is set either way
    bool is_cached(std::string const &path, fs::path &local, uint64_t &expected)
    {
        local = cache_path(path);
        boost::system::error_code ec;
        auto                      it = m_known.find(path);
        expected                     = it != m_known.end() ? it->second.size : 0;
        return fs::exists(local, ec) && (expected == 0 || fs::file_size(local, ec) == expected);
    }

    // Can a SUB_FILE request be answered without reading from the printer?
    bool all_cached(json const &req)
    {
        if (!req.contains("paths")) return false;
        for (auto &p : req["paths"]) {
            std::string path = p.get<std::string>();
            auto        n    = path.find('#');
            std::string sub  = n == std::string::npos ? std::string() : path.substr(n + 1);
            fs::path    local;
            uint64_t    expected = 0;
            // timelapse thumbnails are separate files on the printer
            if (sub.empty() || sub == "thumbnail" || !is_cached(path.substr(0, n), local, expected)) return false;
        }
        return true;
    }

    // Makes sure the model file is available locally, returns 0 on success
    int ensure_cached(std::string const &path, fs::path &local)
    {
        boost::system::error_code ec;
        uint64_t                  expected = 0;
        if (is_cached(path, local, expected))
            return 0;
        fs::path            tmp = local;
        tmp += ".part";
        fs::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) return FILE_READ_WRITE_ERR;
        uint64_t received = 0;
        int      res      = download(path, expected, [&ofs](char const *p, size_t n) {
            ofs.write(p, n);
            return bool(ofs);
        }, received);
        ofs.close();
        if (res != 0 || (expected && received != expected)) {
            fs::remove(tmp, ec);
            return res ? res : ERROR_PIPE;
        }
        fs::rename(tmp, local, ec);
        if (ec) return FILE_READ_WRITE_ERR;
        prune_cache();
        return 0;
    }

    // Reads the given entries of a zip (3mf) file. Missing entries are skipped.
    static bool read_entries(fs::path const &local, std::vector<std::string> const &names, std::vector<std::pair<std::string, std::string>> &out)
    {
        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);
        if (!open_zip_reader(&zip, local.string()))
            return false;
        for (auto &name : names) {
            int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
            if (index < 0) continue;
            size_t size = 0;
            void  *p    = mz_zip_reader_extract_to_heap(&zip, mz_uint(index), &size, 0);
            if (!p) continue;
            out.emplace_back(name, std::string(static_cast<char *>(p), size));
            mz_free(p);
        }
        close_zip_reader(&zip);
        return true;
    }

    static std::string make_zip(std::vector<std::pair<std::string, std::string>> const &entries)
    {
        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);
        if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024))
            return {};
        for (auto &e : entries)
            mz_zip_writer_add_mem(&zip, e.first.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_COMPRESSION);
        void  *buf  = nullptr;
        size_t size = 0;
        std::string out;
        if (mz_zip_writer_finalize_heap_archive(&zip, &buf, &size))
            out.assign(static_cast<char *>(buf), size);
        mz_zip_writer_end(&zip);
        return out;
    }

    static std::string mimetype_of(std::string const &name)
    {
        if (ends_with_ci(name, ".png")) return "image/png";
        if (ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg")) return "image/jpeg";
        if (ends_with_ci(name, ".bmp")) return "image/bmp";
        return "";
    }

    // ---------------------------------------------------------------- job times

    static std::string job_time_key(Entry const &e) { return e.path + "|" + std::to_string(e.size) + "|" + std::to_string(e.time); }

    fs::path job_times_file() const { return cache_dir() / "job_times.json"; }

    // {"<host>": {"<path>|<size>|<printer time>": <job time, 0 = unknown>}}
    void load_job_times()
    {
        try {
            fs::ifstream ifs(job_times_file());
            if (!ifs) return;
            json root = json::parse(ifs);
            if (root.contains(m_host))
                for (auto &item : root[m_host].items())
                    m_job_times[item.key()] = item.value().get<int64_t>();
        } catch (std::exception const &e) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: ignoring job time cache: " << e.what();
        }
    }

    void save_job_times()
    {
        try {
            json root;
            {
                fs::ifstream ifs(job_times_file());
                if (ifs) root = json::parse(ifs, nullptr, false);
                if (!root.is_object()) root = json::object();
            }
            // keep only the files that are still on the printer
            json mine = json::object();
            for (auto &k : m_known) {
                auto it = m_job_times.find(job_time_key(k.second));
                if (it != m_job_times.end()) mine[it->first] = it->second;
            }
            root[m_host] = mine;
            fs::path tmp = job_times_file();
            tmp += ".tmp";
            {
                fs::ofstream ofs(tmp, std::ios::trunc);
                ofs << root.dump(-1, ' ', false, json::error_handler_t::replace);
            }
            boost::system::error_code ec;
            fs::rename(tmp, job_times_file(), ec);
        } catch (std::exception const &e) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: saving job time cache failed: " << e.what();
        }
    }

    // Time to show and sort by: when the job was sliced, else the printer's file time
    time_t job_time(Entry const &e)
    {
        if (!ends_with_ci(e.name, ".3mf")) return e.time;
        auto key = job_time_key(e);
        auto it  = m_job_times.find(key);
        if (it == m_job_times.end()) {
            // a model fetched for its thumbnail before has it locally
            boost::system::error_code ec;
            fs::path                  local = cache_path(e.path);
            if (fs::exists(local, ec) && fs::file_size(local, ec) == e.size) {
                unsigned char buf[ZIP_HEADER_SIZE] = {};
                fs::ifstream  ifs(local, std::ios::binary);
                ifs.read(reinterpret_cast<char *>(buf), sizeof(buf));
                it = m_job_times.emplace(key, int64_t(zip_entry_time(buf, size_t(ifs.gcount())))).first;
                m_job_times_changed = true;
            }
        }
        if (it == m_job_times.end()) {
            if (!m_job_times_disabled) {
                std::lock_guard<std::mutex> l(m_mutex);
                m_pending_times.push_back(e);
            }
            return e.time;
        }
        return it->second > 0 ? time_t(it->second) : e.time;
    }

    // Reads the start of one model file, one at a time while there is nothing else to do
    void fetch_next_job_time()
    {
        Entry e;
        bool  last = false;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (m_pending_times.empty()) return;
            e = m_pending_times.front();
            m_pending_times.pop_front();
            last = m_pending_times.empty();
        }
        m_seq = NO_REQUEST;
        if (m_job_times.find(job_time_key(e)) == m_job_times.end()) {
            std::string head;
            Sink        sink{[&head](char const *p, size_t n) {
                if (head.size() < 64) head.append(p, (std::min)(n, 64 - head.size()));
                return true;
            }};
            prepare(remote_url(e.path));
            // only the first bytes: RETR is aborted after them
            curl_easy_setopt(m_curl, CURLOPT_RANGE, "0-63");
            curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &Tunnel::write_cb);
            curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &sink);
            CURLcode res = curl_easy_perform(m_curl);
            if (head.size() >= ZIP_HEADER_SIZE) {
                m_job_times[job_time_key(e)] = int64_t(zip_entry_time(reinterpret_cast<unsigned char const *>(head.data()), head.size()));
                m_job_times_changed          = true;
                m_job_time_errors            = 0;
            } else if (map_error(res) == ERROR_PIPE) {
                BOOST_LOG_TRIVIAL(info) << "LanFtps: reading start of " << e.path << " failed: " << curl_easy_strerror(res);
                if (++m_job_time_errors >= 3) {
                    // the printer does not like it, use its file times for this session
                    m_job_times_disabled = true;
                    std::lock_guard<std::mutex> l(m_mutex);
                    m_pending_times.clear();
                    last = true;
                }
            } else {
                m_job_times[job_time_key(e)] = 0; // not a zip, nothing to learn
            }
        }
        if (last && m_job_times_changed) {
            m_job_times_changed = false;
            save_job_times();
            // let the storage browser reload the list with the better times
            push_notify(LIST_RESYNC_NOTIFY, json{{"type", "model"}});
        }
    }

    // ---------------------------------------------------------------- requests

    void list_info(uint32_t seq, json const &req)
    {
        std::string                type = req.value("type", "timelapse");
        std::vector<std::string>   dirs;
        std::vector<std::string>   exts;
        if (type == "timelapse") {
            dirs = {"/timelapse"};
            exts = {".mp4", ".avi", ".mov"};
        } else if (type == "model") {
            dirs = {"/", "/cache", "/model"};
            exts = {".3mf"};
        }
        json files = json::array();
        int  ok    = 0;
        int  error = 0;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_pending_times.clear();
        }
        for (auto &dir : dirs) {
            std::vector<Entry> entries;
            int                res = list_dir(dir, entries);
            if (res == ERROR_CANCEL) return;
            if (res != 0) {
                // a missing directory is fine (e.g. no timelapse yet), a dead connection is not
                if (res == ERROR_PIPE) error = res;
                else ++ok;
                continue;
            }
            ++ok;
            for (auto &e : entries) {
                if (e.dir) continue;
                if (std::none_of(exts.begin(), exts.end(), [&e](auto &x) { return ends_with_ci(e.name, x); }))
                    continue;
                m_known[e.path] = e;
                json f;
                f["name"] = e.name;
                f["path"] = e.path;
                f["time"] = job_time(e);
                f["size"] = e.size;
                files.push_back(f);
            }
        }
        if (ok == 0 && error) {
            push(seq, error, json::object());
            return;
        }
        json reply;
        reply["file_lists"] = files;
        push(seq, SUCCESS, reply);
        if (m_job_times_changed && pending_times_empty()) {
            m_job_times_changed = false;
            save_job_times();
        }
    }

    bool pending_times_empty() const
    {
        std::lock_guard<std::mutex> l(m_mutex);
        return m_pending_times.empty();
    }

    void sub_file(uint32_t seq, json const &req)
    {
        bool zip = req.value("zip", false);
        // keep request order, group by file
        std::vector<std::string>                        order;
        std::map<std::string, std::vector<std::string>> groups;
        std::vector<std::string>                        paths;
        for (auto &p : req["paths"]) {
            std::string path = p.get<std::string>();
            paths.push_back(path);
            auto        n    = path.find('#');
            std::string file = path.substr(0, n);
            std::string sub  = n == std::string::npos ? std::string() : path.substr(n + 1);
            if (groups.find(file) == groups.end()) order.push_back(file);
            groups[file].push_back(sub);
        }
        if (paths.empty()) {
            push(seq, FILE_NO_EXIST, json::object());
            return;
        }

        if (zip) {
            for (size_t i = 0; i < order.size(); ++i) {
                if (cancelled()) return;
                auto &file   = order[i];
                int   result = i + 1 == order.size() ? SUCCESS : CONTINUE;
                json  reply;
                reply["path"] = file;
                fs::path local;
                int      res = ensure_cached(file, local);
                if (res == ERROR_CANCEL) return;
                std::vector<std::pair<std::string, std::string>> entries;
                if (res != 0 || !read_entries(local, groups[file], entries) || entries.empty()) {
                    reply["size"] = 0;
                    push(seq, res ? res : FILE_NO_EXIST, reply);
                    continue;
                }
                std::string data = make_zip(entries);
                reply["size"]    = data.size();
                push(seq, result, reply, data.data(), data.size());
            }
            return;
        }

        for (size_t i = 0; i < paths.size(); ++i) {
            if (cancelled()) return;
            auto       &path   = paths[i];
            int         result = i + 1 == paths.size() ? SUCCESS : CONTINUE;
            auto        n      = path.find('#');
            std::string file   = path.substr(0, n);
            std::string sub    = n == std::string::npos ? std::string() : path.substr(n + 1);
            std::string data;
            std::string mimetype;
            int         res = 0;
            if (sub == "thumbnail" || sub.empty()) {
                // timelapse: <dir>/thumbnail/<name without extension>.jpg
                std::string name = file_name_of(file);
                auto        dot  = name.find_last_of('.');
                std::string thumb = dir_of(file) + "/thumbnail/" + (dot == std::string::npos ? name : name.substr(0, dot)) + ".jpg";
                res      = download_to_string(thumb, data);
                mimetype = "image/jpeg";
            } else {
                fs::path local;
                res = ensure_cached(file, local);
                std::vector<std::pair<std::string, std::string>> entries;
                if (res == 0 && read_entries(local, {sub}, entries) && !entries.empty())
                    data = std::move(entries.front().second);
                else if (res == 0)
                    res = FILE_NO_EXIST;
                mimetype = mimetype_of(sub);
            }
            if (res == ERROR_CANCEL) return;
            json reply;
            reply["path"]      = path;
            reply["thumbnail"] = sub;
            reply["mimetype"]  = mimetype;
            reply["size"]      = res == 0 ? data.size() : 0;
            // a missing thumbnail must not stop the remaining ones
            push(seq, res == 0 || result == CONTINUE ? result : res, reply, data.data(), res == 0 ? data.size() : 0);
        }
    }

    void file_download(uint32_t seq, json const &req)
    {
        std::string path = req.contains("path") ? req["path"].get<std::string>() : "/" + req.value("file", "");
        uint64_t    total = 0;
        int         res   = remote_size(path, total);
        if (res != 0) {
            if (res != ERROR_CANCEL) push(seq, res, json::object());
            return;
        }

        MD5_CTX ctx;
        MD5_Init(&ctx);
        std::string pending;
        uint64_t    offset = 0; // offset of the first byte in pending
        auto        flush  = [&](size_t n) {
            json reply;
            reply["offset"] = offset;
            reply["total"]  = total;
            reply["size"]   = n;
            push(seq, CONTINUE, reply, pending.data(), n);
            pending.erase(0, n);
            offset += n;
        };
        auto sink = [&](char const *p, size_t n) {
            if (cancelled()) return false;
            MD5_Update(&ctx, p, n);
            pending.append(p, n);
            // never send the last bytes before the transfer is complete,
            // the final message has to carry the checksum
            while (pending.size() >= 2 * CHUNK_SIZE)
                flush(CHUNK_SIZE);
            return true;
        };

        uint64_t received = 0;
        // use the cached copy if the model was already fetched for its thumbnail
        fs::path                  local = cache_path(path);
        boost::system::error_code ec;
        if (fs::exists(local, ec) && fs::file_size(local, ec) == total && total > 0) {
            fs::ifstream ifs(local, std::ios::binary);
            std::vector<char> buf(CHUNK_SIZE);
            while (ifs && !cancelled()) {
                ifs.read(buf.data(), buf.size());
                if (ifs.gcount() > 0) {
                    received += ifs.gcount();
                    sink(buf.data(), size_t(ifs.gcount()));
                }
            }
            res = cancelled() ? ERROR_CANCEL : 0;
        } else {
            res = download(path, total, sink, received);
        }
        if (res != 0) {
            if (res != ERROR_CANCEL) push(seq, res, json::object());
            return;
        }
        json reply;
        reply["offset"]   = offset;
        reply["total"]    = received; // actual size
        reply["size"]     = pending.size();
        reply["file_md5"] = md5_hex(ctx);
        push(seq, SUCCESS, reply, pending.data(), pending.size());
    }

    void file_delete(uint32_t seq, json const &req)
    {
        std::vector<std::string> paths;
        if (req.contains("paths"))
            for (auto &p : req["paths"]) paths.push_back(p.get<std::string>());
        if (req.contains("delete"))
            for (auto &p : req["delete"]) paths.push_back("/" + p.get<std::string>());
        int result = SUCCESS;
        for (auto &path : paths) {
            fs::path cached = cache_path(path); // before remove() forgets the file
            int      res    = remove(path);
            if (res != 0) {
                result = res;
                continue;
            }
            boost::system::error_code ec;
            fs::remove(cached, ec);
            // timelapse videos come with a thumbnail
            if (path.rfind("/timelapse/", 0) == 0) {
                std::string name = file_name_of(path);
                auto        dot  = name.find_last_of('.');
                remove(dir_of(path) + "/thumbnail/" + (dot == std::string::npos ? name : name.substr(0, dot)) + ".jpg");
            }
        }
        push(seq, result, json::object());
    }

    // ---------------------------------------------------------------- upload

    // PrinterFileSystem uploads in two steps: a FILE_UPLOAD request with name and
    // size, answered with CONTINUE and the fragment size, then FILE_UPLOAD messages
    // with "frag_id" and the bytes of one fragment each, the last one with the MD5
    // of the file. It sends them without waiting for answers, so they are queued
    // here (send_message returns would_block while the queue is full) and streamed
    // into one STOR by the worker thread. The file goes to the root of the storage,
    // where the printer also puts the files sent from Bambu Studio.
    void file_upload(uint32_t seq, json const &req)
    {
        std::string name = file_name_of(req.value("path", ""));
        if (name.empty() || name.find_first_of("\r\n\\") != std::string::npos) {
            push(seq, FILE_NAME_INVALID, json::object());
            return;
        }
        std::string path  = "/" + name;
        uint64_t    total = req.value("total", uint64_t(0));
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_upload        = Upload{};
            m_upload.seq    = seq;
            m_upload.active = true;
            m_upload.total  = total;
            MD5_Init(&m_upload.ctx);
        }
        push(seq, CONTINUE, json{{"chunk_size", UPLOAD_CHUNK_KB}, {"offset", 0}});

        BOOST_LOG_TRIVIAL(info) << "LanFtps: uploading " << path << " (" << total << " bytes)";
        prepare(remote_url(path));
        curl_easy_setopt(m_curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(m_curl, CURLOPT_READFUNCTION, &Tunnel::read_cb);
        curl_easy_setopt(m_curl, CURLOPT_READDATA, this);
        curl_easy_setopt(m_curl, CURLOPT_INFILESIZE_LARGE, curl_off_t(total));
        CURLcode res  = curl_easy_perform(m_curl);
        long     code = 0;
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &code);

        uint64_t    sent;
        bool        stopping;
        std::string md5, expected_md5;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            stopping     = m_stopping;
            sent         = m_upload.sent;
            md5          = md5_hex(m_upload.ctx);
            expected_md5 = m_upload.md5;
            m_upload     = Upload{};
        }
        m_known.erase(path); // the list has to fetch size and time again
        if (stopping) return;

        int result = 0;
        if (cancelled()) {
            result = ERROR_CANCEL;
        } else if (res != CURLE_OK && sent != total) {
            BOOST_LOG_TRIVIAL(info) << "LanFtps: STOR " << path << " failed: " << curl_easy_strerror(res) << " (" << code << ")";
            result = upload_error(res, code);
        } else {
            // also catches a data connection closed early, the printer's SIZE tells what arrived
            uint64_t size = 0;
            if (remote_size(path, size) != 0 || size != total) {
                BOOST_LOG_TRIVIAL(info) << "LanFtps: " << path << " has " << size << " of " << total << " bytes after upload";
                result = SEND_ERR;
            } else if (!expected_md5.empty() && lower(expected_md5) != lower(md5)) {
                result = MD5_COMPARE_ERR;
            }
        }
        if (result != 0) {
            // no partial file on the storage, it would look like a valid job. Nothing
            // sent: STOR was refused and a file of that name is still the old one.
            m_seq = NO_REQUEST;
            if (sent > 0) remove(path);
            if (result != ERROR_CANCEL) push(seq, result, json::object()); // a cancel was answered already
            return;
        }
        BOOST_LOG_TRIVIAL(info) << "LanFtps: uploaded " << path;
        push(seq, SUCCESS, json{{"path", path}});
    }

    int upload_fragment(uint32_t seq, json const &req, std::string data)
    {
        std::lock_guard<std::mutex> l(m_mutex);
        // a fragment of an upload that failed or was cancelled
        if (!m_upload.active || m_upload.seq != seq || m_upload.failed) return 0;
        if (m_upload.queued >= UPLOAD_QUEUE_MAX) return Bambu_would_block;
        if (req.value("offset", uint64_t(0)) != m_upload.received || m_upload.received + data.size() > m_upload.total) {
            BOOST_LOG_TRIVIAL(warning) << "LanFtps: upload fragment out of order";
            m_upload.failed = true;
        } else {
            MD5_Update(&m_upload.ctx, data.data(), data.size());
            m_upload.received += data.size();
            m_upload.queued += data.size();
            if (req.contains("file_md5")) m_upload.md5 = req["file_md5"].get<std::string>();
            m_upload.fragments.push_back(std::move(data));
        }
        m_cond.notify_all();
        return 0;
    }

    static size_t read_cb(char *buffer, size_t size, size_t nitems, void *userdata)
    {
        return static_cast<Tunnel *>(userdata)->read_upload(buffer, size * nitems);
    }

    size_t read_upload(char *buffer, size_t max)
    {
        std::unique_lock<std::mutex> l(m_mutex);
        auto &u     = m_upload;
        auto  ready = [&] { return m_stopping || m_cancelled.count(u.seq) > 0 || u.failed || !u.fragments.empty() || u.sent == u.total; };
        if (!m_cond.wait_for(l, std::chrono::seconds(UPLOAD_WAIT_SECS), ready)) {
            BOOST_LOG_TRIVIAL(warning) << "LanFtps: upload stalled";
            u.failed = true;
        }
        if (m_stopping || m_cancelled.count(u.seq) > 0 || u.failed) return CURL_READFUNC_ABORT;
        if (u.fragments.empty()) return 0; // all sent
        auto  &front = u.fragments.front();
        size_t n     = (std::min)(max, front.size() - u.front_offset);
        memcpy(buffer, front.data() + u.front_offset, n);
        u.front_offset += n;
        u.sent += n;
        u.queued -= n;
        if (u.front_offset == front.size()) {
            u.fragments.pop_front();
            u.front_offset = 0;
        }
        return n;
    }

    int upload_error(CURLcode res, long code) const
    {
        if (cancelled()) return ERROR_CANCEL;
        if (res == CURLE_ABORTED_BY_CALLBACK) return SEND_ERR; // fragments stopped coming or were wrong
        if (code == 452 || code == 552) return STORAGE_SPACE_NOT_ENOUGH;
        if (res == CURLE_UPLOAD_FAILED || res == CURLE_REMOTE_ACCESS_DENIED) return code == 550 || code == 553 ? FILE_CREATE_ERR : FILE_WRITE_ERR;
        int e = map_error(res);
        return e == FILE_NO_EXIST ? FILE_WRITE_ERR : e;
    }

    // ---------------------------------------------------------------- part skip

    // A "get_project_file" request (FILE_DOWNLOAD of "mem:/<n>" with a json param)
    // asks for a file of the job that is printing. The printer's own file service
    // extracts it from the running job. Here it comes from the job's 3mf on the
    // storage, whose possible paths the part skip dialog sends in "lan_project_files".
    void project_file_download(uint32_t seq, json const &param)
    {
        std::string entry = param.value("file_rel_path", "");
        fs::path    local;
        int         res = entry.empty() ? FILE_NO_EXIST : find_project(param.value("lan_project_files", json::array()), local);
        if (res == ERROR_CANCEL) return;
        std::string data;
        if (res == 0 && !read_project_entry(local, entry, data)) res = FILE_NO_EXIST;
        if (res != 0) {
            push(seq, res, json::object());
            return;
        }

        // first message: the result of the request, as the printer sends it
        std::string head = json{{"result", 0}, {"size", data.size()}}.dump();
        json        reply;
        reply["offset"]            = 0;
        reply["total"]             = data.size();
        reply["size"]              = head.size();
        reply["mem_dl_param_size"] = head.size();
        push(seq, CONTINUE, reply, head.data(), head.size());

        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, data.data(), data.size());
        std::string md5 = md5_hex(ctx);
        for (size_t offset = 0; offset < data.size(); offset += CHUNK_SIZE) {
            if (cancelled()) return;
            size_t n    = (std::min)(CHUNK_SIZE, data.size() - offset);
            bool   last = offset + n == data.size();
            json   chunk;
            chunk["offset"] = offset;
            chunk["total"]  = data.size();
            chunk["size"]   = n;
            if (last) chunk["file_md5"] = md5;
            push(seq, last ? SUCCESS : CONTINUE, chunk, data.data() + offset, n);
        }
    }

    // Finds the job's 3mf among the candidate paths and makes it available locally
    int find_project(json const &candidates, fs::path &local)
    {
        // the dialog asks for three files in a row, look the job file up once for them
        auto now = std::chrono::steady_clock::now();
        if (!m_job_path.empty() && m_job_candidates == candidates && now - m_job_found < std::chrono::seconds(30))
            return ensure_cached(m_job_path, local);
        m_job_path.clear();

        std::map<std::string, std::vector<Entry>> listed; // LIST each directory once
        for (auto &c : candidates) {
            if (!c.is_string()) continue;
            if (cancelled()) return ERROR_CANCEL;
            std::string path = c.get<std::string>();
            std::string dir  = dir_of(path);
            auto        it   = listed.find(dir);
            if (it == listed.end()) {
                std::vector<Entry> entries;
                if (list_dir(dir, entries) != 0) entries.clear();
                it = listed.emplace(dir, std::move(entries)).first;
            }
            for (auto &e : it->second) {
                if (e.dir || e.path != path) continue;
                // size and time tell a new upload of the same name from the cached one
                m_known[e.path] = e;
                BOOST_LOG_TRIVIAL(info) << "LanFtps: job file " << path;
                int res = ensure_cached(path, local);
                if (res == 0) {
                    m_job_path       = path;
                    m_job_candidates = candidates;
                    m_job_found      = now;
                }
                return res;
            }
        }
        BOOST_LOG_TRIVIAL(info) << "LanFtps: job file not found on the storage";
        return FILE_NO_EXIST;
    }

    // Reads one entry of the job's 3mf. A sliced file holds the G-code of the plate
    // that is printed under that plate's number, which the printer does not always
    // report in LAN only mode. So the pick image of a plate that is not in the file
    // falls back to the one of the only plate with G-code.
    static bool read_project_entry(fs::path const &local, std::string const &name, std::string &data)
    {
        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);
        if (!open_zip_reader(&zip, local.string()))
            return false;
        int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
        if (index < 0 && name.rfind("Metadata/pick_", 0) == 0) {
            constexpr char const *prefix = "Metadata/plate_";
            constexpr char const *suffix = ".gcode";
            std::vector<std::string> plates;
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
                char buf[256];
                if (mz_zip_reader_get_filename(&zip, i, buf, sizeof(buf)) == 0) continue;
                std::string file = buf;
                if (file.rfind(prefix, 0) == 0 && ends_with_ci(file, suffix))
                    plates.push_back(file.substr(strlen(prefix), file.size() - strlen(prefix) - strlen(suffix)));
            }
            if (plates.size() == 1)
                index = mz_zip_reader_locate_file(&zip, ("Metadata/pick_" + plates.front() + ".png").c_str(), nullptr, 0);
        }
        bool ok = false;
        if (index >= 0) {
            size_t size = 0;
            void  *p    = mz_zip_reader_extract_to_heap(&zip, mz_uint(index), &size, 0);
            if (p) {
                data.assign(static_cast<char *>(p), size);
                mz_free(p);
                ok = size > 0;
            }
        }
        close_zip_reader(&zip);
        return ok;
    }

private:
    std::string m_host;
    std::string m_user;
    std::string m_passwd;
    CURL       *m_curl = nullptr;

    mutable std::mutex      m_mutex;
    std::condition_variable m_cond;
    std::deque<json>        m_requests;
    std::deque<json>        m_deferred; // previews requested while the printer prints
    std::deque<std::string> m_responses;
    std::string             m_current;
    std::set<uint32_t>      m_cancelled;
    bool                    m_stopping = false;
    std::atomic<uint32_t>   m_seq{0};
    std::thread             m_worker;

    std::map<std::string, Entry> m_known; // worker thread only

    // the upload in progress, guarded by m_mutex
    struct Upload
    {
        uint32_t                seq    = NO_REQUEST;
        bool                    active = false;
        bool                    failed = false;
        uint64_t                total    = 0;
        uint64_t                received = 0; // queued by send_message
        uint64_t                sent     = 0; // handed to curl
        size_t                  queued   = 0;
        std::deque<std::string> fragments;
        size_t                  front_offset = 0;
        MD5_CTX                 ctx;
        std::string             md5; // sent with the last fragment
    } m_upload;

    // job file of the part skip dialog, worker thread only
    std::string                           m_job_path;
    json                                  m_job_candidates;
    std::chrono::steady_clock::time_point m_job_found;

    std::map<std::string, int64_t> m_job_times; // worker thread only
    std::deque<Entry>              m_pending_times;
    bool                           m_job_times_changed  = false;
    bool                           m_job_times_disabled = false;
    int                            m_job_time_errors    = 0;
};

Tunnel *as_tunnel(Bambu_Tunnel t) { return static_cast<Tunnel *>(t); }

} // namespace

bool is_url(char const *url) { return url && strncmp(url, SCHEME, strlen(SCHEME)) == 0; }

std::string make_url(std::string const &ip, std::string const &user, std::string const &passwd)
{
    return std::string(SCHEME) + ip + "?user=" + url_encode(user) + "&passwd=" + url_encode(passwd);
}

void set_printer_busy(std::string const &ip, bool busy)
{
    if (ip.empty()) return;
    std::lock_guard<std::mutex> l(g_busy_mutex);
    if (busy)
        g_busy_hosts.insert(ip);
    else
        g_busy_hosts.erase(ip);
}

int create(Bambu_Tunnel *tunnel, char const *url)
{
    if (!is_url(url)) return -1;
    auto t = new Tunnel(url);
    if (!t->valid()) {
        delete t;
        return -1;
    }
    *tunnel = t;
    return 0;
}

int open(Bambu_Tunnel tunnel) { return as_tunnel(tunnel)->open(); }

int start_stream(Bambu_Tunnel, int) { return 0; }

int send_message(Bambu_Tunnel tunnel, int, char const *data, int len) { return as_tunnel(tunnel)->send_message(data, len); }

int read_sample(Bambu_Tunnel tunnel, Bambu_Sample *sample) { return as_tunnel(tunnel)->read_sample(sample); }

void close(Bambu_Tunnel tunnel)
{
    if (tunnel) as_tunnel(tunnel)->stop();
}

void destroy(Bambu_Tunnel tunnel) { delete as_tunnel(tunnel); }

} // namespace LanFtps
} // namespace Slic3r
