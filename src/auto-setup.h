/*
 * auto-setup.h - First-run auto-setup for Minewache scene collection
 *
 * On first OBS launch after plugin install, offers to switch to the
 * pre-configured Minewache scene collection with LTC on Track 3.
 */

#pragma once

#ifdef ENABLE_FRONTEND_API
void auto_setup_init(void);
void auto_setup_cleanup(void);
#endif
