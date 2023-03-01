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


struct mouse_pos_info 
{
    int x;
    int y;
    int max_x;
    int max_y;
};

static int cursor_size = 32;
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

static void draw_box(uint32_t* pixels, struct mydrm_data* data, uint32_t color)
{
    static bool go_right = true;
    static size_t start_x = 0;
    const size_t box_width = 50;
    const size_t box_height = data->height;
    const size_t speed = 5;
    const size_t start_y = 0; 

    if (go_right)
        start_x += speed;
    else
        start_x -= speed;

    if (start_x + box_width >= data->width || start_x <= 0)
        go_right = !go_right;

    for (size_t x = 0; x < box_width; x++)
    {
        for (size_t y = 0; y < box_height; y++)
        {
            size_t pos = (x + start_x) + ((y + start_y) * data->width);

            pixels[pos] = color;
        }
    }
}

void draw_cursor(uint32_t* p, struct mydrm_buf* buf, struct mydrm_data* data)
{
    int start_x = mouse_pos.x;
    int start_y = mouse_pos.y;

    // draw cursor
    for (int x = 0; x < cursor_size; x++)
    {
        for (int y = 0; y < cursor_size; y++)
        {
            int pos = (start_x + x) + ((start_y + y) * data->width);

            if (pos * 4 >= buf->size)
                break; // dont draw past the end buffer

            uint32_t color = cursor_color;

            if (left_down)
                color |= 0xFFFF0000;
            
            if (right_down)
                color |= 0x0000FF00;
            
            p[pos] = color;
        }
    }
}

void draw_hardware_cursor(struct mydrm_data* data)
{
    /*
    // Change cursor color 
    uint32_t hwcursor_color = cursor_color;
    uint32_t* hwcursor_pixels = (uint32_t*)data->hw_cursor.map;

    if (left_down)
        hwcursor_color |= 0xFFFF0000;
    
    if (right_down)
        hwcursor_color |= 0x0000FF00;
    
    for (size_t i = 0; i < data->hw_cursor.size / sizeof(uint32_t); i++)
        if (hwcursor_pixels[i] & 0xFF000000)
            hwcursor_pixels[i] = hwcursor_color;
    */

    if (mydrm_move_cursor(data->fd, data->crt_id, mouse_pos.x, mouse_pos.y) == -1)
        perror("move cursor");
}

static void draw_data(int fd, struct mydrm_data* data)
{
    struct mydrm_buf* buf = &data->framebuffer[data->front_buf ^ 1];

    // `p` for pixels 
    uint32_t* p = (uint32_t*)buf->map;

    memset(p, bg_color, buf->size);

    // position: x + (y * width)
    draw_box(p, data, 0xFFFFFF00);
    draw_hardware_cursor(data);
    // draw_cursor(p, buf, data);

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

int set_mode(struct mydrm_data* data, struct drm_mode_get_connector* conn, struct drm_mode_modeinfo* mode, struct drm_mode_card_res* res)
{
    bool running = true;
    int nfds = 0;
    int epfd = 0;
    int ret = 0;
    int mouse_fd = 0;
    struct drm_mode_get_encoder enc;
    struct drm_mode_crtc crtc;
    struct mydrm_event_context ev;
    struct epoll_event event;
    struct epoll_event events[3];

    ret = mydrm_get_encorder(data->fd, conn->encoder_id, &enc);

    if ((enc.encoder_id != conn->encoder_id && enc.crtc_id == 0) || ret == -1)
    {
        for (size_t i = 0; i < conn->count_encoders; i++)
        {
            ret = mydrm_get_encorder(data->fd, ((uint32_t*)conn->encoders_ptr)[i], &enc);

            if (ret == -1)
                continue;

            for (size_t j = 0; j < res->count_crtcs; j++)
            {
                if (enc.possible_crtcs & (1 << j))
                {
                    enc.crtc_id = ((uint32_t*)res->crtc_id_ptr)[j];
                    goto encoder_found;
                }
            }
        }
        goto drop_master;
    }
encoder_found:

    if (!enc.crtc_id)
    {
        fprintf(stderr, "No CRT controller!\n");
        ret = -1;
        goto drop_master;
    }

    data->framebuffer[0].width = mode->hdisplay;
    data->framebuffer[0].height = mode->vdisplay;

    data->framebuffer[1].width = mode->hdisplay;
    data->framebuffer[1].height = mode->vdisplay;

    mouse_pos.x = (mode->hdisplay / 2) - cursor_size;
    mouse_pos.y = (mode->vdisplay / 2) - cursor_size;

    if (!mydrm_create_framebuffer(data->fd, &data->framebuffer[0]))
    {
        fprintf(stderr, "Failed to create framebuffer 1!\n");
        ret = -1;
        goto drop_master;
    }

    if (!mydrm_create_framebuffer(data->fd, &data->framebuffer[1]))
    {
        fprintf(stderr, "Failed to create framebuffer 2!\n");
        ret = -1;
        goto drop_master;
    }

    printf("Buffer created with size: %d bytes\n", data->framebuffer[0].size);

    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = enc.crtc_id;
    data->crt_id = enc.crtc_id;

    ret = mydrm_setup_hardware_cursor(data);
    
    if (ret != -1)
        printf("Hardware Cursor:\n\tW/H: %dx%d\n\bo_handle: %d\n", data->hw_cursor.width, data->hw_cursor.height, data->hw_cursor.handle);

    ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_GETCRTC, &crtc);

    if (ret == -1)
    {
        perror("ioctl DRM_IOCTL_MODE_GETCRTC");
        goto unmap_fb;
    }

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

    ret = open("/dev/input/mice", O_RDONLY);

    if (ret == -1)
    {
        perror("open /dev/input/mice");
        goto unmap_fb;
    }
    mouse_fd = ret;
    
    printf("mouse fd: %d\n", mouse_fd);
    printf("CRTC res: %d/%d @ %d Hz...\n", crtc.mode.hdisplay, crtc.mode.vdisplay, crtc.mode.vrefresh);
    mouse_pos.max_x = crtc.mode.hdisplay;
    mouse_pos.max_y = crtc.mode.vdisplay;

    // set mode...
    ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);

    if (ret == -1)
    {
        perror("ioctl: Failed to set CRTC");
        goto unmap_fb;
    }


    draw_data(data->fd, data);

    if ((ret = epoll_create1(0)) == -1)
    {
        perror("epoll_create1");
        goto unmap_fb;
    }
    epfd = ret;

    event.events = EPOLLIN;
    event.data.fd = 0;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event) == -1)
    {
        perror("epoll_ctl 0");
        goto unmap_fb;
    }

    event.data.fd = data->fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, data->fd, &event) == -1)
    {
        perror("epoll_ctl data->fd");
        goto unmap_fb;
    }

    event.data.fd = mouse_fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, mouse_fd, &event) == -1)
    {
        perror("epoll_ctl data->fd");
        goto unmap_fb;
    }

    memset(&ev, 0, sizeof(ev));
    ev.version = 2;
    ev.page_flip_handler = page_flip_event;

    while (running)
    {
        if ((nfds = epoll_wait(epfd, events, 3, -1)) == -1)
        {
            if (errno == EINTR)
                continue;
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

unmap_fb:
    if (munmap(data->framebuffer[0].map, data->framebuffer[0].size) == -1)
        perror("munmap framebuffer[0]");
    if (munmap(data->framebuffer[1].map, data->framebuffer[1].size) == -1)
        perror("munmap framebuffer[1]");

drop_master:
    if (mouse_fd && (close(mouse_fd) == -1))
        perror("close mouse_fd");
    if (epfd && (close(epfd) == -1))
        perror("close epfd");

    printf("DONE!!!\n");
    return ret;
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
    
    printf("\tName: %s\n\tDesc: %s\n\tDate: %s\n\n", version->name, version->desc, version->date);

    free(version->name);
    free(version->desc);
    free(version->date);
    free(version);
}


int main(int argc, const char** argv)
{
    printf("drmlist v0.1\n");
    char* drm_path = getenv("DRMLIST_PATH");
    if (!drm_path)
        drm_path = "/dev/dri/card0"; // default path


    int fd = mydrm_open(drm_path);
    if (fd == -1)
        return fd;

    printf("%s (fd: %d):\n", drm_path, fd);
    print_drm_info(fd);
    
    struct drm_mode_card_res res;

    if (mydrm_get_res(fd, &res))
    {
        fprintf(stderr, "Failed to open card0 res\n");
        return -1;
    }

    const char* conn_str = NULL;
    int hres = -1;
    int vres = -1;
    int ret = 0;
    bool is_master = false;

    if (argc == 4)
    {
        conn_str = argv[1];
        hres = atoi(argv[2]);
        vres = atoi(argv[3]);

        if (ioctl(fd, DRM_IOCTL_SET_MASTER, 0) == -1)
            perror("ioctl DRM_IOCTL_SET_MASTER");
        else
            is_master = true;
    }

    struct mydrm_data data = {
        .cleanup= false,
        .pflip_pending = false,
        .front_buf = 0,
        .width = hres,
        .height = vres,
        .fd = fd
    };

    printf("DRM Connectors: %d\n", res.count_connectors);

    for (int i = 0; i < res.count_connectors; i++)
    {
        uint32_t* connectors = (uint32_t*)res.connector_id_ptr;

        struct drm_mode_get_connector conn;
        ret = mydrm_get_connector(fd, connectors[i], &conn);

        if (ret) 
        {
            printf("\t Failed to get connector: %d\n", ret);
            continue;
        }

        const char* conn_type = mydrm_connector_typename(conn.connector_type);

        printf("Connector %d: %s -- ID: %d, Modes: %d, Encoder COUNT/ID: %d/%d\n",
                            i, conn_type, connectors[i], conn.count_modes, conn.count_encoders, conn.encoder_id);

        if (conn.connection != DRM_MODE_CONNECTED)
        {
            if (conn.connection == DRM_MODE_DISCONNECTED)
            {
                printf("\tDisconnected. (connection: %d)\n", conn.connection);
            }
            else
            {
                printf("\tUnknown. (connection: %d)\n", conn.connection);
            }
            goto free_mem;
        }

        struct drm_mode_modeinfo* modes = (struct drm_mode_modeinfo*)conn.modes_ptr;

        for (int m = 0; m < conn.count_modes; m++)
        {
            printf("\t%dx%d @ %dHz\n", modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh);

            if (conn_str && !strcmp(conn_str, conn_type) && hres == modes[m].hdisplay && vres == modes[m].vdisplay)
            {
                printf(">> Attemping to set res: %dx%d on %s\n", hres, vres, conn_type);
                ret = set_mode(&data, &conn, &modes[m], &res);
                goto drop_master;
            }
        }

    free_mem:
        free((void*)conn.props_ptr);
    }

drop_master:
    if (is_master && ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == -1)
        perror("ioctl drop master");
    
    if (close(fd) == -1)
        perror("close");

    return ret;
}
