#ifndef _MY_DRM_H_
#define _MY_DRM_H_

#include <stdint.h>
#include <stdbool.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum mydrm_modes 
{
    DRM_MODE_CONNECTED = 1,
    DRM_MODE_DISCONNECTED = 2,
    DRM_MODE_UNKNOWN = 3
};

struct mydrm_event_context 
{
    int version;
    void (*vblank_handler)(int fd, uint32_t sequence, uint32_t tv_sec, uint32_t tv_usec, void* user_data);
    void (*page_flip_handler)(int fd, uint32_t sequence, uint32_t tv_sec, uint32_t tc_usec, void* user_data);
};

int mydrm_ioctl(int fd, uint64_t request, void* arg);
int mydrm_open(const char* dev_path);
int mydrm_get_res(int fd, struct drm_mode_card_res* res);
int mydrm_get_connector(int fd, int id, struct drm_mode_get_connector* conn);

int mydrm_get_encorder(int fd, int id, struct drm_mode_get_encoder* enc);
int mydrm_handle_event(int fd, struct mydrm_event_context* ctx);

#endif // _MY_DRM_H_