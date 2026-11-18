#include "gs_iop.h"

namespace iop::gs_iop {

void register_servers() {}

void start(const std::string&) {
    log_write(LOG_IOP, "GS_IOP is not present in the selected disc image");
}

}  // namespace iop::gs_iop