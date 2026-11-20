#include "LanPrintHistory.hpp"

#include <cstddef>
#include <ctime>
#include <exception>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace LanPrintHistory {

namespace fs = boost::filesystem;
using nlohmann::json;

namespace {

// keeps the file small; at a few prints a day this is years of history
constexpr size_t MAX_PRINTS = 2000;

bool is_running(std::string const &state) { return state == "PREPARE" || state == "RUNNING" || state == "SLICING"; }

fs::path history_path() { return fs::path(data_dir()) / "print_history.json"; }

// storage file name of the job: the base name of gcode_file when it is a project file
std::string job_file_name(std::string const &gcode_file)
{
    if (!boost::algorithm::iends_with(gcode_file, ".3mf"))
        return {};
    size_t slash = gcode_file.find_last_of("/\\");
    return slash == std::string::npos ? gcode_file : gcode_file.substr(slash + 1);
}

struct History
{
    bool                          loaded = false;
    std::vector<Print>            prints; // oldest first
    std::map<std::string, time_t> starts; // dev_id -> start of the running print

    void load()
    {
        if (loaded)
            return;
        loaded = true;
        boost::system::error_code ec;
        if (!fs::exists(history_path(), ec))
            return;
        try {
            fs::ifstream in(history_path());
            json         root = json::parse(in);
            for (auto const &p : root.value("prints", json::array())) {
                Print print;
                print.dev_id   = p.value("dev_id", "");
                print.file     = p.value("file", "");
                print.subtask  = p.value("subtask", "");
                print.start    = p.value("start", (long long) 0);
                print.end      = p.value("end", (long long) 0);
                print.finished = p.value("finished", false);
                prints.push_back(print);
            }
        } catch (std::exception const &e) {
            BOOST_LOG_TRIVIAL(warning) << "BambuLocal: print history not readable: " << e.what();
        }
    }

    void save() const
    {
        json list = json::array();
        for (auto const &p : prints)
            list.push_back({{"dev_id", p.dev_id},
                            {"file", p.file},
                            {"subtask", p.subtask},
                            {"start", (long long) p.start},
                            {"end", (long long) p.end},
                            {"finished", p.finished}});
        fs::path                  path = history_path();
        fs::path                  tmp  = path.string() + ".tmp";
        boost::system::error_code ec;
        {
            fs::ofstream out(tmp, std::ios::trunc);
            out << json{{"version", 1}, {"prints", list}}.dump(1);
            if (!out) {
                BOOST_LOG_TRIVIAL(warning) << "BambuLocal: print history not writable: " << tmp;
                return;
            }
        }
        fs::rename(tmp, path, ec);
        if (ec)
            BOOST_LOG_TRIVIAL(warning) << "BambuLocal: print history not saved: " << ec.message();
    }
};

History &history()
{
    static History h;
    return h;
}

} // namespace

void on_print_state(std::string const &dev_id, std::string const &from, std::string const &to,
                    std::string const &gcode_file, std::string const &subtask)
{
    History &h = history();
    if (is_running(to) && !is_running(from) && from != "PAUSE") {
        // the first report after connecting starts from an empty state: the start is unknown
        if (from.empty())
            h.starts.erase(dev_id);
        else
            h.starts[dev_id] = std::time(nullptr);
        return;
    }
    if ((to != "FINISH" && to != "FAILED") || !(is_running(from) || from == "PAUSE"))
        return;

    h.load();
    Print print;
    print.dev_id   = dev_id;
    print.file     = job_file_name(gcode_file);
    print.subtask  = subtask;
    print.end      = std::time(nullptr);
    print.finished = to == "FINISH";
    if (auto it = h.starts.find(dev_id); it != h.starts.end()) {
        print.start = it->second;
        h.starts.erase(it);
    }
    if (print.file.empty() && print.subtask.empty())
        return;
    h.prints.push_back(print);
    if (h.prints.size() > MAX_PRINTS)
        h.prints.erase(h.prints.begin(), h.prints.begin() + (h.prints.size() - MAX_PRINTS));
    h.save();
    BOOST_LOG_TRIVIAL(info) << "BambuLocal: print history: " << (print.file.empty() ? print.subtask : print.file) << " "
                            << to << ", " << (print.start ? print.end - print.start : 0) << " s";
}

std::optional<Print> last_print(std::string const &dev_id, std::string const &file_name)
{
    History &h = history();
    h.load();
    for (auto it = h.prints.rbegin(); it != h.prints.rend(); ++it) {
        if (it->dev_id != dev_id)
            continue;
        if (!it->file.empty() ? boost::algorithm::iequals(it->file, file_name) :
                                (boost::algorithm::iequals(it->subtask + ".gcode.3mf", file_name) ||
                                 boost::algorithm::iequals(it->subtask + ".3mf", file_name)))
            return *it;
    }
    return std::nullopt;
}

} // namespace LanPrintHistory
} // namespace Slic3r
