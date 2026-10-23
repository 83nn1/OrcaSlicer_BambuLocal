#pragma once

// LAN-only storage browsing for Bambu Lab printers that do not offer the
// local file service of the network plug-in (e.g. P1 / A1 series).
//
// This module emulates the small "tunnel" API that PrinterFileSystem uses
// (create / open / send message / read sample / close / destroy) and answers
// the same JSON requests (LIST_INFO, SUB_FILE, FILE_DOWNLOAD, FILE_DEL,
// TASK_CANCEL, ...) by talking to the printer's storage over implicit FTPS
// (port 990, user "bblp", password = LAN access code).
//
// Because the request / response protocol is identical, the rest of the
// storage browser (ImageGrid, MediaFilePanel, PrinterFileSystem) works
// unchanged.
//
// The part skip dialog uses the same tunnel: its "get_project_file" requests
// (pick image, model_settings.config, slice_info.config of the running job)
// are answered from the job's 3mf on the storage.

#include <string>

#ifndef BAMBU_DYNAMIC
#define BAMBU_DYNAMIC
#endif
#include "BambuTunnel.h"

namespace Slic3r {
namespace LanFtps {

// URL scheme handled by this module.
// Format: lanftps://<ip>?user=<user>&passwd=<access code>
bool        is_url(char const *url);
std::string make_url(std::string const &ip, std::string const &user, std::string const &passwd);

// While the printer at ip prints, previews (SUB_FILE) that are not cached yet and
// the background job time reads wait, so the printer is not kept busy with FTPS.
void set_printer_busy(std::string const &ip, bool busy);

// Tunnel API, mirrors the Bambu_* functions of the network plug-in.
int  create(Bambu_Tunnel *tunnel, char const *url);
int  open(Bambu_Tunnel tunnel);
int  start_stream(Bambu_Tunnel tunnel, int type);
int  send_message(Bambu_Tunnel tunnel, int ctrl, char const *data, int len);
int  read_sample(Bambu_Tunnel tunnel, Bambu_Sample *sample);
void close(Bambu_Tunnel tunnel);
void destroy(Bambu_Tunnel tunnel);

} // namespace LanFtps
} // namespace Slic3r
