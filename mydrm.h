#ifndef _MY_DRM_H_
#define _MY_DRM_H_

#include <stdint.h>
#include <stdbool.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libdrm/drm_fourcc.h>

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

struct mydrm_buf 
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size;
    uint32_t handle;
    uint8_t* map; /* pixels */
    uint32_t fb;
};

struct mydrm_data 
{
    int fd;
    struct mydrm_buf framebuffer[2];
    struct mydrm_buf hw_cursor;
    uint32_t crt_id;
    bool pflip_pending;
    bool cleanup;
    int front_buf;
    uint32_t width;
    uint32_t height;
};

int mydrm_ioctl(int fd, uint64_t request, void* arg);
int mydrm_open(const char* dev_path);
int mydrm_get_res(int fd, struct drm_mode_card_res* res);
int mydrm_get_connector(int fd, int id, struct drm_mode_get_connector* conn);

int mydrm_get_encorder(int fd, int id, struct drm_mode_get_encoder* enc);
int mydrm_handle_event(int fd, struct mydrm_event_context* ctx);

bool mydrm_create_framebuffer(int fd, struct mydrm_buf* buf);

// Free functions
void mydrm_free_connector(struct drm_mode_get_connector* conn);
void mydrm_free_res(struct drm_mode_card_res* res);

// Hardware cursor
int mydrm_setup_hardware_cursor(struct mydrm_data* data);
int mydrm_set_cursor(int fd, uint32_t crtc_id, uint32_t bo_handle, uint32_t width, uint32_t height);
int mydrm_move_cursor(int fd, uint32_t crtc_id, int x, int y);

#endif // _MY_DRM_H_
