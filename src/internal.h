#ifndef XLD_RIPPER_INTERNAL_H
#define XLD_RIPPER_INTERNAL_H

#include <xld/ripper.h>
#include "upstream/XLDCDDABackend.h"

xld_ripper_status ripper_error(xld_ripper_error *error, xld_ripper_status status, int cause, int lsn);
xld_ripper_status ripper_io_status(int cause);
/* Transfers the backend descriptor on success. */
xld_ripper_status ripper_session_create(xld_cdread_t *disc, xld_ripper_session **out);
void ripper_accuraterip_id(const xld_cdread_t *disc, xld_ripper_disc_info *info);

#endif
