#include "sh7058_candidate_link.h"
#include "serial_port_actions.h"
CommunicationResult SH7058CandidateLink::submit(const QByteArray &bytes) { return serial_.write_checked(bytes); }
CommunicationResult SH7058CandidateLink::receive(uint16_t timeout) { return serial_.read_checked(timeout); }
