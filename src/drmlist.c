#include "drmlist.h"
#include <immintrin.h>
#include "drmlist_draw_box.h"

static int hres = -1;
static int vres = -1;

static const char* drm_path = NULL;
static const char* connector_str = NULL;

static mydrm_data_t* data = NULL;
static struct drm_mode_card_res* res = NULL;

struct drm_mode_crtc saved_crtc;

static bool is_master = false;

static void print_drm_info(int fd)
{
    printf("%s (fd: %d):\n", drm_path, fd);

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

int drmlist_init(int argc, const char** argv)
{
    int fd;
    int ret;
    char* cursor_size_str;
    char* no_hw_cursor_str;

    drm_path = getenv(ENV_DRMLIST_DRM_PATH);
    if (!drm_path)
        drm_path = DRMLIST_DRM_DEFAULT;
    
    if ((fd = mydrm_open(drm_path)) == -1)
    {
        char errmsg[PATH_MAX];
        snprintf(errmsg, PATH_MAX, "Failed to open %s", drm_path);
        perror(errmsg);
        return -1;
    }

    if ((ret = mydrm_check_cap(fd)) != 0)
    {
        perror("Unsupported DRM device");
        return ret;
    }

    print_drm_info(fd);

    if (argc == 4)
    {
            connector_str = argv[1];
        hres = atoi(argv[2]);
        vres = atoi(argv[3]);

        if ((ret = mydrm_set_master(fd)) == -1)
            perror("Set Master");
        else
            is_master = true;
    }

    if ((res = malloc(sizeof(struct drm_mode_card_res))) == NULL)
        return -ENOMEM;

    if ((ret = mydrm_get_res(fd, res)))
    {
        perror("Failed to get res");
        return ret;
    }

    if ((data = malloc(sizeof(mydrm_data_t))) == NULL)
        return -ENOMEM;

    memset(data, 0, sizeof(mydrm_data_t));

    data->cleanup = false;
    data->pflip_pending = false;
    data->front_buf = 0;
    data->width = hres;
    data->height = vres;
    data->fd = fd;
    data->bg_color = DRMLIST_BACKGROUND_COLOR;

    if ((data->mouse = malloc(sizeof(mouse_t))) == NULL)
        return -ENOMEM;        

    memset(data->mouse, 0, sizeof(mouse_t));
    if ((cursor_size_str = getenv(ENV_DRMLIST_CURSOR_SIZE)))
        data->mouse->size = atoi(cursor_size_str);
    else
        data->mouse->size = CURSOR_SIZE;

    if ((no_hw_cursor_str = getenv(ENV_DRMLIST_HARDWARE_CURSOR)))
        data->mouse->is_hardware_cursor = !atoi(no_hw_cursor_str);
    else
        data->mouse->is_hardware_cursor = true;

    return ret;
}

static bool drmlist_print_connector(int i, uint32_t* connectors, struct drm_mode_get_connector* conn, const char** conn_type)
{
    *conn_type = mydrm_connector_typename(conn->connector_type);

    printf("Connector %d: %s -- ID: %d, Modes: %d, Encoder COUNT/ID: %d/%d\n",
                        i, *conn_type, connectors[i], conn->count_modes, 
                        conn->count_encoders, conn->encoder_id);
    
    switch (conn->connection)
    {
        case DRM_MODE_CONNECTED:
            return true;
        case DRM_MODE_DISCONNECTED:
            printf("\tDisconnected\n");
            break;
        default:
            printf("\tUnknown (conn->connection: %d)\t", conn->connection);
            break;
    }
    
    return false;
}

static struct drm_mode_modeinfo* drmlist_print_modes_and_get(const char* conn_str, const char* conn_type, struct drm_mode_get_connector* conn)
{
    int idx = -1;
    struct drm_mode_modeinfo* modes = (struct drm_mode_modeinfo*)conn->modes_ptr;

    for (size_t m = 0; m < conn->count_modes; m++)
    {
        if (idx == -1 && conn_str && !strcmp(conn_str, conn_type) && hres == modes[m].hdisplay && vres == modes[m].vdisplay)
        {
            printf("  >>>>");
            idx = m;
        }

        printf("\t%dx%d @ %dHz\n", modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh);
    }

    return (idx == -1) ? NULL : &modes[idx];
}

static bool drmlist_create_fbs(struct drm_mode_modeinfo* mode)
{
    data->framebuffer[0].width = mode->hdisplay;
    data->framebuffer[0].height = mode->vdisplay;

    data->framebuffer[1].width = mode->hdisplay;
    data->framebuffer[1].height = mode->vdisplay;

    if (!mydrm_create_framebuffer(data->fd, &data->framebuffer[0]))
    {
        fprintf(stderr, "Failed to create framebuffer[0]\n");
        return false;
    }

    if (!mydrm_create_framebuffer(data->fd, &data->framebuffer[1]))
    {
        fprintf(stderr, "Failed to create framebuffer[1]\n");
        return false;
    }

    printf("FrameBuffer created with size: %d bytes\n", data->framebuffer[0].size);

    return true;
}

static int drmlist_get_encoder(struct drm_mode_get_connector* conn, struct drm_mode_get_encoder* enc)
{
    int ret;

    ret = mydrm_get_encorder(data->fd, conn->encoder_id, enc);

    if ((enc->encoder_id != conn->encoder_id && enc->crtc_id == 0) || ret == -1)
    {
        for (size_t i = 0; i < conn->count_encoders; i++)
        {
            if ((ret = mydrm_get_encorder(data->fd, ((uint32_t*)conn->encoders_ptr)[i], enc)) == -1)
                continue;
            
            for (size_t j = 0; j < res->count_crtcs; j++)
            {
                if (enc->possible_crtcs & (1 << j))
                {
                    enc->crtc_id = ((uint32_t*)res->crtc_id_ptr)[j];
                    goto encoder_found;
                }
            }
        }
        return -1;
    }
encoder_found:;

    if (!enc->crtc_id)
    {
        fprintf(stderr, "No CRT controller!\n");
        return ret;
    }

    data->crt_id = enc->crtc_id;

    return ret;
}

static void drmlist_mv_hw_cursor(mydrm_data_t* data, mydrm_fb_t* fb) 
{
    if (!data->mouse->moved)
        return;
    if (mydrm_move_cursor(data->fd, data->crt_id, data->mouse->x, data->mouse->y) == -1)
        perror("move cursor");
    data->mouse->moved = false;
}

static void drmlist_mv_sw_cursor(mydrm_data_t* data, mydrm_fb_t* fb)
{
    mouse_t* mouse = data->mouse;
    int start_x = mouse->x;
    int start_y = mouse->y;

    // draw cursor
    for (int x = 0; x < mouse->size; x++)
    {
        for (int y = 0; y < mouse->size; y++)
        {
            int pos = (start_x + x) + ((start_y + y) * data->width);

            if (pos * 4 >= fb->size)
                 break; // dont draw past the end buffer

            uint32_t color = mouse->color;


            if (mouse->left_down)
                color |= 0x00FF0000;
            
            if (mouse->right_down)
                color |= 0x0000FF00;
            
            fb->pixels[pos] = color;
        }
    }
}

static int drmlist_mouse_init(struct drm_mode_modeinfo* mode)
{
    int ret = 0;
    mouse_t* mouse = data->mouse;

    // Begin the mouse at the middle of the screen
    mouse->x = (mode->hdisplay / 2) - mouse->size;
    mouse->y = (mode->vdisplay / 2) - mouse->size;

    mouse->max_x = mode->hdisplay;
    mouse->max_y = mode->vdisplay;

    mouse->color = 0xFF0000FF;

    if ((mouse->fd = open("/dev/input/mice", O_RDONLY)) == -1)
    {
        perror("FAILED to open /dev/input/mice");
        return -1;
    }

    if (mouse->is_hardware_cursor && (ret = mydrm_setup_hardware_cursor(data)) != -1)
    {
        printf("Hardware Cursor:\n\tsize: %dx%d\n\tbo_handle: %d\n", mouse->hw_cursor_fb->height, mouse->hw_cursor_fb->height, mouse->hw_cursor_fb->handle);
        mouse->is_hardware_cursor = true;
        mouse->move_cursor_callback = drmlist_mv_hw_cursor;
    }
    else 
    {
        printf("Using software cursor\n");
        mouse->is_hardware_cursor = false;
        mouse->move_cursor_callback = drmlist_mv_sw_cursor;
        ret = 0;
    }

    return ret;
}

static int drmlist_get_crtc(struct drm_mode_get_encoder* enc, struct drm_mode_crtc* crtc)
{
    int ret;

    memset(crtc, 0, sizeof(struct drm_mode_crtc));
    memset(&saved_crtc, 0, sizeof(struct drm_mode_crtc));
    crtc->crtc_id = enc->crtc_id;
    saved_crtc.crtc_id = enc->crtc_id;
    data->crt_id = enc->crtc_id;

    
    if ((ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_GETCRTC, &saved_crtc)) == -1)
        perror("ioctl DRM_IOCTL_MODE_GETCRTC");
    else
        printf("Saved crtc id: %d, ret: %d (fb_id: %d, count_connectors: %d, ptr: %llx, name: %s)\n", saved_crtc.crtc_id, ret, 
                                saved_crtc.fb_id, saved_crtc.count_connectors, 
                                saved_crtc.set_connectors_ptr, saved_crtc.mode.name);
    return ret;
}

static int drmlist_save_crtc(struct drm_mode_get_encoder* enc)
{
    int ret;
    memset(&saved_crtc, 0, sizeof(struct drm_mode_crtc));

    saved_crtc.crtc_id = enc->crtc_id;
    
    if ((ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_GETCRTC, &saved_crtc)) == -1)
        perror("ioctl DRM_IOCTL_MODE_GETCRTC");

    return ret;
}

static int drmlist_set_crtc(mydrm_data_t* data, struct drm_mode_crtc* crtc, struct drm_mode_get_encoder* enc, struct drm_mode_modeinfo* mode, struct drm_mode_get_connector* conn)
{
    int ret;

    memset(crtc, 0, sizeof(struct drm_mode_crtc));
    memcpy(&crtc->mode, mode, sizeof(struct drm_mode_modeinfo));

    crtc->crtc_id = enc->crtc_id;
    crtc->x = 0;
    crtc->y = 0;
    crtc->fb_id = data->framebuffer[0].fb;
    crtc->count_connectors = 1;
    crtc->set_connectors_ptr = (uint64_t)&conn->connector_id;
    crtc->mode_valid = 1;

    if ((ret  = mydrm_set_crtc(data->fd, crtc)))
        perror("ioctl DRM_IOCTL_MODE_SETCRTC");

    return 0;
}

static int drmlist_init_mode(struct drm_mode_get_connector* conn, struct drm_mode_modeinfo* mode)
{
    struct drm_mode_get_encoder enc;
    struct drm_mode_crtc crtc;
    int ret;

    if ((ret = drmlist_get_encoder(conn, &enc))) 
        return ret;

    if (!drmlist_create_fbs(mode))
        return -1;

    if ((ret = drmlist_mouse_init(mode)))
        return ret;

    if ((ret = drmlist_save_crtc(&enc)))
        return ret;

    /* Set Mode */
    if ((ret = drmlist_set_crtc(data, &crtc, &enc, mode, conn)) == -1)
        return ret;

    return ret;

set_mode_error:;
}

static int drmlist_init_epoll(int* epfd, struct epoll_event* event)
{
    if ((*epfd = epoll_create1(0)) == -1)
    {
        perror("FAILED epoll_create1");
        return -1;
    }

    event->events = EPOLLIN;
    event->data.fd = 0;     // stdin
    if (epoll_ctl(*epfd, EPOLL_CTL_ADD, 0, event) == -1)
    {
        perror("FAILED EPOLL_CTL_ADD 0");
        return -1;
    }

    event->data.fd = data->fd;

    if (epoll_ctl(*epfd, EPOLL_CTL_ADD, data->fd, event) == -1)
    {
        perror("FAILED EPOLL_CTL_ADD data->fd");
        return -1;
    }

    event->data.fd = data->mouse->fd;

    if (epoll_ctl(*epfd, EPOLL_CTL_ADD, data->mouse->fd, event) == -1)
    {
        perror("FAILED EPOLL_CTL_ADD data->mouse->fd");
        return -1;
    }

    return 0;
}

static bool go_right = true;
static size_t start_x = 0;
static const size_t start_y = 0; 
static const size_t box_width = 32;
static const size_t box_height = 32;
static const size_t speed = 5;


static void drmlist_draw_box_avx2(uint32_t* pixels, mydrm_data_t* data, uint32_t color)
{
    const size_t box_height = data->height;

    __m256i color_vec = _mm256_set1_epi32(color);

    if (go_right)
        start_x += speed;
    else
        start_x -= speed;

    if (start_x + box_width >= data->width || start_x <= 0)
        go_right = !go_right;

    for (size_t y = 0; y < box_height; y++)
    {
        for (size_t x = 0; x < box_width; x += 8)
        {
            size_t pos = (x + start_x) + ((y + start_y) * data->width);

            __m256i pixels_vec = _mm256_loadu_si256((__m256i*)&pixels[pos]);

            pixels_vec = _mm256_blendv_epi8(pixels_vec, color_vec, _mm256_set1_epi32(0xFFFFFFFF));

            _mm256_storeu_si256((__m256i*)&pixels[pos], pixels_vec);
        }
    }
}

static void drmlist_draw_box(uint32_t* pixels, mydrm_data_t* data, uint32_t color)
{
    const size_t box_height = data->height;

    if (go_right)
        start_x += speed;
    else
        start_x -= speed;

    if (start_x + box_width >= data->width || start_x <= 0)
        go_right = !go_right;

    for (size_t y = 0; y < box_height; y++)
    {
        for (size_t x = 0; x < box_width; x++)
        {
            size_t pos = (x + start_x) + ((y + start_y) * data->width);

            if (pos * 4 >= data->framebuffer[data->front_buf].size)
                break;

            pixels[pos] = color;
        }
    }
}

static void drmlist_flip_page(mydrm_data_t* data, mydrm_fb_t* fb)
{
    int ret;
    struct drm_mode_crtc_page_flip flip;
    flip.fb_id = fb->fb;
    flip.crtc_id = data->crt_id; 
    flip.user_data = (uint64_t)data;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    flip.reserved = 0;

    ret = mydrm_ioctl(data->fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);

    if (!ret)
    {
        data->pflip_pending = true;
        data->front_buf ^= 1;
    }
    else
    {
        perror("FAILED ioctl DRM_IOCTL_MODE_PAGE_FLIP");
    }
}

static void drmlist_draw_data(int fd, mydrm_data_t* data)
{
    mydrm_fb_t* fb = &data->framebuffer[data->front_buf ^ 1];
    uint32_t* pixels = (uint32_t*)fb->pixels;

    /* Make all pixels backgroud color */
    memset(pixels, data->bg_color, fb->size) ;

    /* Update box */
    drmlist_draw_box_asm(pixels, data, 0xFFFF0000);
    
    /* Update cursor */
    data->mouse->move_cursor_callback(data, fb);

    drmlist_flip_page(data, fb);
}

static void drmlist_page_flip_event(int fd, uint32_t sequence, uint32_t tv_sec, uint32_t tv_usec, void* user_data)
{
    data->pflip_pending = false;

    if (!data->cleanup)
        drmlist_draw_data(fd, data);
}

static int drmlist_epoll_wait(int epfd, struct epoll_event* events, size_t n_events)
{
    int ret;
    if ((ret = epoll_wait(epfd, events, n_events, -1)) == -1)
        perror("epoll_wait");
    return ret;
}

static void drmlist_handle_mouse_event(mydrm_data_t* data)
{
    mouse_t* mouse = data->mouse;
    char buffer[3];

    if (read(data->mouse->fd, buffer, 3) == -1)
        perror("read data->mouse->fd");

    mouse->x += buffer[1];
    mouse->y -= buffer[2];
    mouse->left_down = buffer[0] & 1;
    mouse->right_down = buffer[0] & 2;

    if (mouse->x > mouse->max_x - 1)
        mouse->x = mouse->max_x - 1;
    else if (mouse->x < 0)
        mouse->x = 0;

    if (mouse->y > mouse->max_y - 1)
        mouse->y = mouse->max_y - 1;
    else if (mouse->y < 0)
        mouse->y = 0;

    mouse->moved = true;
}

static int drmlist_mainloop(mydrm_data_t* data)
{
    bool running = true;
    int ret;
    int epfd; // epoll fd
    int nfds;
    struct epoll_event event; 
    struct epoll_event events[3];
    mydrm_event_context_t ev;

    if ((ret = drmlist_init_epoll(&epfd, &event)))
        return ret;

    memset(&ev, 0, sizeof(mydrm_event_context_t));
    ev.version = 2;
    ev.page_flip_handler = drmlist_page_flip_event;

    drmlist_draw_data(data->fd, data);

    while (running)
    {
        if ((nfds = drmlist_epoll_wait(epfd, events, 3)) == -1)
            return -1;

        for (int i = 0; i < nfds; i++)
        {
            int ready_fd = events[i].data.fd;


            if (ready_fd == 0)
                running = false;
            else if (ready_fd == data->fd)
                mydrm_handle_event(data->fd, &ev);
            else if (ready_fd == data->mouse->fd)
                drmlist_handle_mouse_event(data);
        }
    }

    return ret;
}

int drmlist_run(void)
{
    int ret;

    printf("DRM Connectors: %d\n", res->count_connectors);

    for (size_t i = 0; i < res->count_connectors; i++)
    {
        struct drm_mode_get_connector conn;
        struct drm_mode_modeinfo* mode = NULL;
        uint32_t* connectors = (uint32_t*)res->connector_id_ptr;
        const char* conn_type;

        if ((ret = mydrm_get_connector(data->fd, connectors[i], &conn)))
            return ret;

        if (drmlist_print_connector(i, connectors, &conn, &conn_type))
            mode = drmlist_print_modes_and_get(connector_str, conn_type, &conn);
        
        if (mode)
        {
            ret = drmlist_init_mode(&conn, mode);
            if (ret == 0) 
                ret = drmlist_mainloop(data);

            mydrm_free_connector(&conn);
            break;
        }

        mydrm_free_connector(&conn);
    }

    return ret;
}

void drmlist_cleanup(void)
{

    free(res);
    free(data);
}
