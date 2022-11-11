#include "mydrm.h"
#include <libdrm/drm_mode.h>
#include <libdrm/drm.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <dirent.h>
#include <linux/limits.h>

struct mydrm_buf 
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size;
    uint32_t handle;
    uint8_t* map;
    uint32_t fb;
};

struct mydrm_data 
{
    int fd;
    struct mydrm_buf framebuffer[2];
    uint32_t crt_id;
    bool pflip_pending;
    bool cleanup;
    int front_buf;
    uint32_t width;
    uint32_t height;
};

struct mouse_pos_info 
{
    int x;
    int y;
    int max_x;
    int max_y;
};

int cursor_size = 20;
uint32_t bg_color = 0xFF111111;
                   /* AARRGGBB */
uint32_t cursor_color = 0xFF0000FF;

bool left_down = false;
bool right_down = false;

struct mouse_pos_info mouse_pos;
struct drm_mode_crtc saved_crtc;

const char* mydrm_connector_typename (uint32_t connector_type)
{
	/* Keep the strings in sync with the kernel's drm_connector_enum_list in
	 * drm_connector.c. */
	switch (connector_type) 
    {
        case DRM_MODE_CONNECTOR_Unknown:
            return "Unknown";
        case DRM_MODE_CONNECTOR_VGA:
            return "VGA";
        case DRM_MODE_CONNECTOR_DVII:
            return "DVI-I";
        case DRM_MODE_CONNECTOR_DVID:
            return "DVI-D";
        case DRM_MODE_CONNECTOR_DVIA:
            return "DVI-A";
        case DRM_MODE_CONNECTOR_Composite:
            return "Composite";
        case DRM_MODE_CONNECTOR_SVIDEO:
            return "SVIDEO";
        case DRM_MODE_CONNECTOR_LVDS:
            return "LVDS";
        case DRM_MODE_CONNECTOR_Component:
            return "Component";
        case DRM_MODE_CONNECTOR_9PinDIN:
            return "DIN";
        case DRM_MODE_CONNECTOR_DisplayPort:
            return "DP";
        case DRM_MODE_CONNECTOR_HDMIA:
            return "HDMI-A";
        case DRM_MODE_CONNECTOR_HDMIB:
            return "HDMI-B";
        case DRM_MODE_CONNECTOR_TV:
            return "TV";
        case DRM_MODE_CONNECTOR_eDP:
            return "eDP";
        case DRM_MODE_CONNECTOR_VIRTUAL:
            return "Virtual";
        case DRM_MODE_CONNECTOR_DSI:
            return "DSI";
        case DRM_MODE_CONNECTOR_DPI:
            return "DPI";
        case DRM_MODE_CONNECTOR_WRITEBACK:
            return "Writeback";
        case DRM_MODE_CONNECTOR_SPI:
            return "SPI";
        case DRM_MODE_CONNECTOR_USB:
            return "USB";
        default:
            return NULL;
	}
}


bool create_framebuffer(int fd, struct mydrm_buf* buf)
{
    struct drm_mode_create_dumb creq;
    struct drm_mode_create_dumb dreq;
    struct drm_mode_map_dumb mreq;

    memset(&creq, 0, sizeof(creq));
    creq.width = buf->width;
    creq.height = buf->height;
    creq.bpp = 32;

    int ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);

    if (ret < 0)
    {
        perror("ioctl failed to create the buffer");

        return false;
    }

    buf->stride = creq.pitch;
    buf->size = creq.size;
    buf->handle = creq.handle;

    struct drm_mode_fb_cmd fbcmd;
    memset(&fbcmd, 0, sizeof(fbcmd));
    fbcmd.width = buf->width;
    fbcmd.height = buf->height;
    fbcmd.depth = 24;
    fbcmd.bpp = 32;
    fbcmd.pitch = buf->stride;
    fbcmd.handle = buf->handle;

    ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbcmd);

    if (ret < 0)
    {
        perror("ioctl failed to add fb");
        return false;
    }

    buf->fb = fbcmd.fb_id;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = buf->handle;

    ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq); 

    if (ret < 0)
    {
        perror("ioctl failed to map");
        return false;
    }

    buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);

    if (buf->map == MAP_FAILED)
    {
        perror("mmap: failed to map fb");
        return false;
    }

    memset(buf->map, 0, buf->size);

    return true;
}

static void draw_data(int fd, struct mydrm_data* data)
{
    struct mydrm_buf* buf = &data->framebuffer[data->front_buf ^ 1];

    int start_x = mouse_pos.x;
    int start_y = mouse_pos.y;

    uint32_t* p = (uint32_t*)buf->map;

    memset(p, bg_color, buf->size);

    for (int x = 0; x < cursor_size; x++)
    {
        for (int y = 0; y < cursor_size; y++)
        {
            int pos = (start_x + x) + ((start_y + y) * data->width);

            if (pos * 4 >= buf->size)
                break; // dont draw past the end buffer

            uint32_t color = cursor_color;

            if (left_down)
                color |= 0x00FF0000;
            
            if (right_down)
                color |= 0x0000FF00;
            
            p[pos] = color;
        }
    }

    struct drm_mode_crtc_page_flip flip;
    flip.fb_id = buf->fb;
    flip.crtc_id = data->crt_id;
    flip.user_data = (uint64_t)data;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    flip.reserved = 0;

    int ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);

    if (!ret)
    {
        data->pflip_pending = true;
        data->front_buf ^= 1;
    }
    else
    {
        perror("ioctl: Failed to flip");
    }
}

static void page_flip_event(int fd, uint32_t frame, uint32_t sec, uint32_t usec, void* data)
{
    struct mydrm_data* dev = data;
    dev->pflip_pending = false;

    if (!dev->cleanup)
        draw_data(fd, dev);
}

int set_mode(struct mydrm_data* data, struct drm_mode_get_connector* conn, struct drm_mode_modeinfo* mode)
{
    if (!conn->encoder_id)
    {
        fprintf(stderr, "No encorder found!\n");
        // return -1;
    }

    struct drm_mode_get_encoder enc;

    if (mydrm_get_encorder(data->fd, conn->encoder_id, &enc))
    {
        fprintf(stderr, "Encorer load failed!\n");
        return -1;
    }

    if (!enc.crtc_id)
    {
        fprintf(stderr, "No CRT controller!\n");
        return -1;
    }

    data->framebuffer[0].width = mode->hdisplay;
    data->framebuffer[0].height = mode->vdisplay;

    data->framebuffer[1].width = mode->hdisplay;
    data->framebuffer[1].height = mode->vdisplay;

    mouse_pos.x = 0;
    mouse_pos.y = 0;

    if (!create_framebuffer(data->fd, &data->framebuffer[0]))
    {
        fprintf(stderr, "Failed to create framebuffer 1!\n");
        return -1;
    }

    if (!create_framebuffer(data->fd, &data->framebuffer[1]))
    {
        fprintf(stderr, "Failed to create framebuffer 2!\n");
        return -1;
    }

    printf("Buffer created with size: %d\n", data->framebuffer[0].size);

    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = enc.crtc_id;

    data->crt_id = enc.crtc_id;

    int ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_GETCRTC, &crtc);

    saved_crtc = crtc;

    printf("Get crtc: %d = %d (%d, %d, %llx, %s)\n", crtc.crtc_id, ret, crtc.fb_id, crtc.count_connectors, crtc.set_connectors_ptr, crtc.mode.name);

    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = enc.crtc_id;
    memcpy(&crtc.mode, mode, sizeof(*mode));
    crtc.x = 0;
    crtc.y = 0;
    crtc.fb_id = data->framebuffer[0].fb;
    crtc.count_connectors = 1;
    crtc.set_connectors_ptr = (uint64_t)&conn->connector_id;
    crtc.mode_valid = 1;

    int mouse_fd = open("/dev/input/mice", O_RDONLY);

    if (mouse_fd == -1)
        perror("open /dev/input/mice");
    
    printf("mouse fd: %d\n", mouse_fd);
    printf("CRTC res: %d/%d...\n", crtc.mode.hdisplay, crtc.mode.vdisplay);
    mouse_pos.max_x = crtc.mode.hdisplay;
    mouse_pos.max_y = crtc.mode.vdisplay;
    
    // sleep(4);

    // set mode...
    ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);

    if (ret)
    {
        perror("ioctl: Failed to set CRTC");
        return ret;
    }

    sleep(1);

    draw_data(data->fd, data);

    int epfd = -1;
    if ((epfd = epoll_create1(0)) == -1)
    {
        perror("epoll_create1");
        return -1;
    }

    struct epoll_event event;
    struct epoll_event events[3];
    event.events = EPOLLIN;
    event.data.fd = 0;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event) == -1)
    {
        perror("epoll_ctl 0");
        return -1;
    }

    event.data.fd = data->fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, data->fd, &event) == -1)
    {
        perror("epoll_ctl data->fd");
        return -1;
    }

    event.data.fd = mouse_fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, mouse_fd, &event) == -1)
    {
        perror("epoll_ctl data->fd");
        return -1;
    }

    struct mydrm_event_context ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = 2;
    ev.page_flip_handler = page_flip_event;

    int nfds = 0;

    bool running = true;

    while (running)
    {
        if ((nfds = epoll_wait(epfd, events, 3, -1)) == -1)
        {
            perror("epoll_wait");
        }

        for (int i = 0; i < nfds; i++)
        {

            int tmp_fd = events[i].data.fd;

            if (tmp_fd == 0)
            {
                running = false;
            }
            else if (tmp_fd == data->fd)
            {
                mydrm_handle_event(data->fd, &ev);
            }
            else if (tmp_fd == mouse_fd)
            {
                char buffer[3];
                int r = read(mouse_fd, buffer, 3);

                mouse_pos.x += buffer[1];
                mouse_pos.y -= buffer[2];
                left_down = buffer[0] & 1;
                right_down = buffer[0] & 2;

                if (mouse_pos.x > mouse_pos.max_x - cursor_size)
                    mouse_pos.x = mouse_pos.max_x - cursor_size;
                else if (mouse_pos.x < 0)
                    mouse_pos.x = 0;
                
                if (mouse_pos.y > mouse_pos.max_y - cursor_size)
                    mouse_pos.y = mouse_pos.max_y - cursor_size;
                else if (mouse_pos.y < 0)
                    mouse_pos.y = 0;
                
                // printf("MOUSE: %d %d %d\n", buffer[0], buffer[1], buffer[2]);
            }
        }
    }

    saved_crtc.count_connectors = 1;
    saved_crtc.mode_valid = 1;
    saved_crtc.set_connectors_ptr = (uint64_t)&conn->connector_id;

    ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_SETCRTC, &saved_crtc);

    if (ret == -1)
        perror("ioctl set crtc");

    if (ioctl(data->fd, DRM_IOCTL_DROP_MASTER, 0) == -1)
        perror("ioctl drop master");
    
    if (close(data->fd) == -1)
        perror("close data->fd");
    
    if (close(mouse_fd) == -1)
        perror("close mouse_fd");

    if (close(epfd) == -1)
        perror("close epfd");
    
    if (munmap(data->framebuffer[0].map, data->framebuffer[0].size) == -1)
        perror("munmap framebuffer[0]");
    if (munmap(data->framebuffer[1].map, data->framebuffer[1].size) == -1)
        perror("munmap framebuffer[1]");

    printf("DONE!!!\n");

    return 0;
}

void print_drm_info(int fd)
{
    struct drm_version* version = malloc(sizeof(struct drm_version));
    memset(version, 0, sizeof(struct drm_version));

    if (ioctl(fd, DRM_IOCTL_VERSION, version) == -1)
        perror("ioctl DRM_IOCTL_VERSION(1)");
    
    if (version->name_len)
    {
        version->name = malloc(version->name_len + 1);
        memset(version->name, 0, version->name_len + 1);
    }
    if (version->desc_len)
    {
        version->desc = malloc(version->desc_len + 1);
        memset(version->desc, 0, version->desc_len + 1);
    }
    if (version->date_len)
    {
        version->date = malloc(version->date_len + 1);
        memset(version->date, 0, version->date_len + 1);
    }

    if (ioctl(fd, DRM_IOCTL_VERSION, version) == -1)
        perror("ioctl DRM_IOCTL_VERSION(2)");
    
    printf("Name: %s\nDesc: %s\nDate: %s\n", version->name, version->desc, version->date);

    free(version->name);
    free(version->desc);
    free(version->date);
    free(version);
}


int main(int argc, const char** argv)
{
    printf("DRM Modes:\n");

    int fd = mydrm_open("/dev/dri/card0");
    if (fd == -1)
        return fd;

    printf("drm_fd: %d\n", fd);
    print_drm_info(fd);
    
    struct drm_mode_card_res res;

    if (mydrm_get_res(fd, &res))
    {
        fprintf(stderr, "Failed to open card0 res\n");
        return -1;
    }

    int hres = 0;
    int vres = 0;

    if (argc == 3)
    {
        hres = atoi(argv[1]);
        vres = atoi(argv[2]);

        printf("Attemping to set res: %dx%d\n", hres, vres);
    }

    struct mydrm_data data = {
        .cleanup= false,
        .pflip_pending = false,
        .front_buf = 0,
        .width = hres,
        .height = vres,
        .fd = fd
    };

    if (ioctl(fd, DRM_IOCTL_SET_MASTER, 0) == -1)
        perror("ioctl set master");

    printf("DRM Connectors: %d\n", res.count_connectors);

    for (int i = 0; i < res.count_connectors; i++)
    {
        uint32_t* connectors = (uint32_t*)res.connector_id_ptr;

        struct drm_mode_get_connector conn;
        int ret = mydrm_get_connector(fd, connectors[i], &conn);

        if (ret) 
        {
            printf("\t Failed to get connector: %d\n", ret);
            continue;
        }

        // printf("Found connector: %d - %d. Modes %d (%s)\n", i, connectors[i], conn.count_modes, drm_connector_str_list[conn.connector_type]);
        printf("Found connector: %s: %d - %d. Modes %d\n",
                            mydrm_connector_typename(conn.connector_type), i, connectors[i], conn.count_modes);

        if (conn.connection != DRM_MODE_CONNECTED)
        {
            if (conn.connection == DRM_MODE_DISCONNECTED)
            {
                printf("\tDisconnected. (%d)\n", conn.connection);
            }
            else
            {
                printf("\tUnknown. (%d)\n", conn.connection);
            }
            continue;
        }

        struct drm_mode_modeinfo* modes = (struct drm_mode_modeinfo*)conn.modes_ptr;

        for (int m = 0; m < conn.count_modes; m++)
        {
            printf("\tMode: %dx%d\n", modes[m].hdisplay, modes[m].vdisplay);

            if (hres == modes[m].hdisplay && vres == modes[m].vdisplay)
            {
                return set_mode(&data, &conn, &modes[m]);
            }
        }
    }

    if (ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == -1)
        perror("ioctl drop master");

    return 0;
}
