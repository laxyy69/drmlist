#ifndef _MY_DRM_
#define _MY_DRM_

#include <drm.h>
#include <drm_mode.h>
#include <drm_fourcc.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>

#include <linux/limits.h>

enum mydrm_modes 
{
    DRM_MODE_CONNECTED = 1,
    DRM_MODE_DISCONNECTED = 2,
    DRM_MODE_UNKNOWN = 3
};

typedef struct 
{
    int version;
    void (*vblank_handler)(int fd, uint32_t sequence, uint32_t tv_sec, uint32_t tv_usec, void* user_data);
    void (*page_flip_handler)(int fd, uint32_t sequence, uint32_t tv_sec, uint32_t tc_usec, void* user_data);
} mydrm_event_context_t;

/*
 * mydrm_fb_t - MyDRM Framebuffer
 */
typedef struct 
{
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;
    uint32_t size;
    uint32_t handle;
    uint32_t fb;
} mydrm_fb_t; 

typedef struct mouse mouse_t;

typedef struct 
{
    mydrm_fb_t framebuffer[2];
    mouse_t* mouse;

    uint32_t width;
    uint32_t height;
    uint32_t crt_id;
    uint32_t bg_color;
    int fd;
    int front_buf;

    bool pflip_pending;
    bool cleanup;
} mydrm_data_t;

typedef struct mouse
{
    void (*move_cursor_callback)(mydrm_data_t* data, mydrm_fb_t* fb);
    mydrm_fb_t* hw_cursor_fb;
    uint32_t color;
    int size;
    int x;
    int y;
    int max_x;
    int max_y;
    int fd;

    bool is_hardware_cursor;
    bool left_down;
    bool right_down;
    bool moved;
} mouse_t;


int mydrm_open(const char* dev_path);
int mydrm_ioctl(int fd, uint64_t request, void* arg);
const char* mydrm_connector_typename (uint32_t connector_type);
int mydrm_check_cap(int fd);
int mydrm_handle_event(int fd, mydrm_event_context_t* ctx);

bool mydrm_create_framebuffer(int fd, mydrm_fb_t* fb);

// Set/Drop master
int mydrm_set_master(int fd);
int mydrm_drop_master(int fd);

// Gets
int mydrm_get_res(int fd, struct drm_mode_card_res* res);
int mydrm_get_encorder(int fd, int id, struct drm_mode_get_encoder* enc);
int mydrm_get_connector(int fd, int id, struct drm_mode_get_connector* conn);

// Sets
int mydrm_set_crtc(int fd, struct drm_mode_crtc* crtc);

// Free functions
void mydrm_free_res(struct drm_mode_card_res* res);
void mydrm_free_connector(struct drm_mode_get_connector* conn);

// Hardware cursor
int mydrm_setup_hardware_cursor(mydrm_data_t* data);
int mydrm_move_cursor(int fd, uint32_t crtc_id, int x, int y);
int mydrm_set_cursor(int fd, uint32_t crtc_id, uint32_t bo_handle, uint32_t width, uint32_t height);



#endif // _MY_DRM_
