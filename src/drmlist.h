#ifndef _DRMLIST_H_
#define _DRMLIST_H_

#include "mydrm/mydrm.h"

#define ENV_DRMLIST_DRM_PATH "DRMLIST_PATH"
#define ENV_DRMLIST_CURSOR_SIZE "DRMLIST_CURSOR_SIZE"
#define ENV_DRMLIST_HARDWARE_CURSOR "DRMLIST_NO_HW_CURSOR"

#define DRMLIST_DRM_DEFAULT "/dev/dri/card0"
#define CURSOR_SIZE 32
#define DRMLIST_BACKGROUND_COLOR 0xFF111111

int drmlist_init(int argc, const char** argv);
int drmlist_run(void);
void drmlist_cleanup(void);

#endif // _DRMLIST_H_H
