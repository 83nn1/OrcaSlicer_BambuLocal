#pragma once

// Local print history.
//
// Without the Bambu cloud there is no record of which file was printed when and how it
// ended, and the printer's clock is unreliable in LAN-only mode. Orca therefore records
// every print it sees end (gcode_state leaves a running job for FINISH or FAILED) with
// the PC's clock, in <data_dir>/print_history.json, and the Storage tab shows the last
// print of each file on its card.

#include <ctime>
#include <optional>
#include <string>

namespace Slic3r {
namespace LanPrintHistory {

struct Print
{
    std::string dev_id;
    std::string file;    // file name of the job on the storage, e.g. "Benchy.gcode.3mf"; may be empty
    std::string subtask; // job name reported by the printer
    time_t      start = 0; // 0 when Orca did not see the print start
    time_t      end   = 0;
    bool        finished = false; // false: cancelled or failed
};

// Called on every gcode_state change of a printer, on the GUI thread.
void on_print_state(std::string const &dev_id, std::string const &from, std::string const &to,
                    std::string const &gcode_file, std::string const &subtask);

// Storage file name of a job: the base name of the printer's gcode_file when it is a project file, else empty.
std::string job_file_name(std::string const &gcode_file);

// Whether the storage file file_name is the job with this job file name (see job_file_name), or, when that
// is empty, with the job name subtask.
bool is_job_file(std::string const &file_name, std::string const &job_file, std::string const &subtask);

// The last recorded print of the storage file file_name on printer dev_id.
std::optional<Print> last_print(std::string const &dev_id, std::string const &file_name);

} // namespace LanPrintHistory
} // namespace Slic3r
