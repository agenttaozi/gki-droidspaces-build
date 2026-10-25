/* Started by AICoder, pid:4a30ef9a2dl93a914bed0aabf088d91c11d22f10 */
#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_BOOTLOADER_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_BOOTLOADER_H

#include "eswin_eph861x_types.h"

extern int eph_chg_force_bootloader(struct eph_data *ephdata);
extern int eph_bootloader_release_chg(struct eph_data *ephdata);
extern int eph_send_frames(struct eph_data *ephdata);
extern int eph_app_force_bootloader_mode(struct eph_data *ephdata);
extern int eph_check_bootloader(struct eph_data *ephdata, unsigned int expected_next_state);

#endif /* __LINUX_PLATFORM_DATA_ESWIN_EPH_BOOTLOADER_H */
/* Ended by AICoder, pid:4a30ef9a2dl93a914bed0aabf088d91c11d22f10 */