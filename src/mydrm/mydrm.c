#include "mydrm.h"

int mydrm_open(const char* dev_path)
{
    return open(dev_path, O_RDWR | O_CLOEXEC);
}

int mydrm_ioctl(int fd, unsigned long request, void* arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

const char* mydrm_connector_typename(uint32_t connector_type)
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

/* 
 *  Checks if DRM device support "dumb buffer"
 */
int mydrm_check_cap(int fd)
{
    struct drm_get_cap get_cap = {
        .capability = DRM_CAP_DUMB_BUFFER,
        .value = 0
    };

    if (mydrm_ioctl(fd, DRM_IOCTL_GET_CAP, &get_cap) == -1 || !get_cap.value)
        return -EOPNOTSUPP;
    return 0;
}

/*
 * Handle DRM event, by reading from DRM device
 */
int mydrm_handle_event(int fd, mydrm_event_context_t* ctx)
{
    uint8_t buffer[1024];
    struct drm_event* e;

    int len = read(fd, buffer, sizeof(buffer));

    if (len == -1)
    {
        perror("read");
        return -1;
    }

    if (len < sizeof(struct drm_event))
        return -1;
    
    int i = 0;
    while (i < len)
    {
        e = (struct drm_event*)&buffer[i];
        i += e->length;

        switch (e->type)
        {
            case DRM_EVENT_FLIP_COMPLETE:
            {
                struct drm_event_vblank* vb = (struct drm_event_vblank*)e;
                ctx->page_flip_handler(fd, vb->sequence, vb->tv_sec, vb->tv_usec, (void*)vb->user_data);
                break;
            }
            default:
            {
                printf("Event: %d\n", e->type);
                break;
            }
        }
    }

    return 0;
}

/*
 * Create a dumb buffer
 */
static int mydrm_create_dumb_buffer(int fd, struct drm_mode_create_dumb* creq, mydrm_fb_t* fb)
{
    int ret;

    memset(creq, 0, sizeof(struct drm_mode_create_dumb));

    creq->width = fb->width;
    creq->height = fb->height;
    creq->bpp = fb->bpp;

    ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, creq);

    return ret;
}

/*
 * Create a mew framebuffer object (FBO)
 */
static int mydrm_add_fb(int fd, mydrm_fb_t* fb)
{
    int ret;
    struct drm_mode_fb_cmd fbcmd;
    memset(&fbcmd, 0, sizeof(fbcmd));

    fbcmd.width = fb->width;
    fbcmd.height = fb->height;
    fbcmd.depth = 24;
    fbcmd.bpp = fb->bpp;
    fbcmd.pitch = fb->stride;
    fbcmd.handle = fb->handle;

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbcmd)) < 0)
    {
        perror("ioctl DRM_IOCTL_MODE_ADDFB");
        return ret;
    }

    fb->fb = fbcmd.fb_id;
    return ret;
}

/*
 * Map dumb buffer into process's address space
 */
static int mydrm_map_buffer(int fd, mydrm_fb_t* fb)
{
    int ret;
    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(struct drm_mode_map_dumb));

    mreq.handle = fb->handle;

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) < 0)
        return ret;

    if ((fb->pixels= mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset)) == MAP_FAILED)
        return -1;
    
    memset(fb->pixels, 0, fb->size);

    return 0;
}

/* 
 * Create dumb buffer, FBO and map it = FrameBuffer
 * 
 *  Don't get confused with 'fd' and 'fb'
 * `fd` - file descriptor 
 * `fb` - FrameBuffer
 */
bool mydrm_create_framebuffer(int fd, mydrm_fb_t* fb)
{
    int ret;
    struct drm_mode_create_dumb creq;

    if (fb->bpp == 0)
        fb->bpp = 32; // default

    if ((ret = mydrm_create_dumb_buffer(fd, &creq, fb)) < 0)
    {
        perror("ioctl DRM_IOCTL_MODE_CREATE_DUMB");
        return false;
    }

    fb->stride = creq.pitch;
    fb->size = creq.size;
    fb->handle = creq.handle;

    printf("Creating framebuffer: %dx%d, size: %llu\n", creq.width, creq.height, creq.size);

    if ((ret = mydrm_add_fb(fd, fb)) < 0)
    {
        perror("ioctl DRM_IOCTL_MODE_ADDFB");
        return false;
    }


    if ((ret = mydrm_map_buffer(fd, fb)) < 0)
    {
        perror("FAILED to map fb buffer");
        return false;
    }

    return true;
}

/*
 * Set/Drop master
 */
int mydrm_set_master(int fd)
{
    return mydrm_ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
}

int mydrm_drop_master(int fd)
{
    return mydrm_ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
}

int mydrm_get_res(int fd, struct drm_mode_card_res* res)
{
    int ret;
    memset(res, 0, sizeof(struct drm_mode_card_res));

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res)))
    {
        perror("ioctl DRM_IOCTL_MODE_GETRESOURCES (1)");
        return ret;
    }

    if (res->count_fbs)
    {
        res->fb_id_ptr = (uint64_t)malloc(res->count_fbs * sizeof(uint32_t));
        if (res->fb_id_ptr == 0)
            return -ENOMEM;
        memset((void*)res->fb_id_ptr, 0, res->count_fbs * sizeof(uint32_t));
    }

    if (res->count_crtcs)
    {
        res->crtc_id_ptr = (uint64_t)malloc(res->count_crtcs * sizeof(uint32_t));
        if (res->crtc_id_ptr == 0)
            return -ENOMEM;
        memset((void*)res->crtc_id_ptr, 0, res->count_crtcs * sizeof(uint32_t));
    }

    if (res->count_connectors)
    {
        res->connector_id_ptr = (uint64_t)malloc(res->count_connectors * sizeof(uint32_t));
        if (res->connector_id_ptr == 0)
            return -ENOMEM;
        memset((void*)res->connector_id_ptr, 0, res->count_connectors * sizeof(uint32_t));
    }

    if (res->count_encoders)
    {
        res->encoder_id_ptr = (uint64_t)malloc(res->count_encoders * sizeof(uint32_t));
        if (res->encoder_id_ptr == 0)
            return -ENOMEM;
        memset((void*)res->encoder_id_ptr, 0, res->count_encoders * sizeof(uint32_t));
    }

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res)))
        perror("ioctl DRM_IOCTL_MODE_GETRESOURCES (2)");

    return ret;
}

int mydrm_get_connector(int fd, int id, struct drm_mode_get_connector* conn)
{
    int ret;
    memset(conn, 0, sizeof(struct drm_mode_get_connector));
    conn->connector_id = id;
    conn->count_modes = 0;

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn)))
    {
        perror("ioctl DRM_IOCTL_MODE_GETCONNECTOR (1)");
        return ret;
    }

    if (conn->count_props)
    {
        conn->props_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint32_t));
        if (conn->props_ptr == 0)
            return -ENOMEM;
        memset((void*)conn->props_ptr, 0, conn->count_props * sizeof(uint32_t));
        conn->prop_values_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint64_t));
        if (conn->prop_values_ptr == 0)
            return -ENOMEM;
        memset((void*)conn->prop_values_ptr, 0, conn->count_props * sizeof(uint64_t));
    }

    if (conn->count_modes)
    {
        conn->modes_ptr = (uint64_t)malloc(conn->count_modes * sizeof(struct drm_mode_modeinfo));
        if (conn->modes_ptr == 0)
            return -ENOMEM;
        memset((void*)conn->modes_ptr, 0, conn->count_modes * sizeof(struct drm_mode_modeinfo));
    }

    if (conn->count_encoders)
    {
        conn->encoders_ptr = (uint64_t)malloc(conn->count_encoders * sizeof(uint32_t));
        if (conn->encoders_ptr == 0)
            return -ENOMEM;
        memset((void*)conn->encoders_ptr, 0, conn->count_encoders * sizeof(uint32_t));
    }

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn)))
        perror("ioctl DRM_IOCTL_MODE_GETCONNECTOR (2)");

    return ret;
}

int mydrm_get_encorder(int fd, int id, struct drm_mode_get_encoder* enc)
{
    memset(enc, 0, sizeof(struct drm_mode_get_encoder));

    int ret;
    enc->encoder_id = id;

    if ((ret = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, enc)))
        perror("ioctl DRM_IOCTL_MODE_GETENCODER");
    
    return ret;
}

/*
 * Sets
 */

int mydrm_set_crtc(int fd, struct drm_mode_crtc* crtc)
{
    return mydrm_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, crtc);
}

/*
 * Free functions
 */
void mydrm_free_res(struct drm_mode_card_res* res)
{
    free((void*)res->fb_id_ptr);
    free((void*)res->crtc_id_ptr);
    free((void*)res->connector_id_ptr);
    free((void*)res->encoder_id_ptr);
}

void mydrm_free_connector(struct drm_mode_get_connector* conn)
{
    free((void*)conn->props_ptr);
    free((void*)conn->prop_values_ptr);
    free((void*)conn->modes_ptr);
    free((void*)conn->encoders_ptr);
}

/*
 * Hardware cursor functions
 */
/*
 * Set cursor
 */
int mydrm_set_cursor(int fd, uint32_t crtc_id, uint32_t bo_handle, uint32_t width, uint32_t height)
{
    struct drm_mode_cursor arg;
    memset(&arg, 0, sizeof(struct drm_mode_cursor));

    arg.flags = DRM_MODE_CURSOR_BO;
    arg.crtc_id = crtc_id;
    arg.width = width;
    arg.height = height;
    arg.handle = bo_handle;

    return mydrm_ioctl(fd, DRM_IOCTL_MODE_CURSOR, &arg);
}

/*
 * Move cursor
 */
int mydrm_move_cursor(int fd, uint32_t crtc_id, int x, int y)
{
    struct drm_mode_cursor arg;
    memset(&arg, 0, sizeof(struct drm_mode_cursor));

    arg.flags = DRM_MODE_CURSOR_MOVE;
    arg.crtc_id = crtc_id;
    arg.x = x;
    arg.y = y;

    return mydrm_ioctl(fd, DRM_IOCTL_MODE_CURSOR, &arg);
}

// BAD! Hard coded!
size_t load_image(const char* path, void** data)
{
    size_t size = 0;
    int fd = open(path, O_RDONLY);

    if (fd == -1)
    {
        perror("open");
        return 0;
    }
    
    struct stat stat;
    if (fstat(fd, &stat) == -1)
    {
        perror("fstat");
        close(fd);
        return 0;
    }
    size = stat.st_size;

    *data = malloc(size);
    memset(*data, 0, size);

    if (read(fd, *data, size) == -1)
    {
        perror("read");
        close(fd);
        return 0;
    }

    close(fd);

    return size;
}

/*
 * Setup hardware cursor, load 32x32 raw cursor image in
 */
int mydrm_setup_hardware_cursor(mydrm_data_t* data)
{
    mouse_t* mouse = data->mouse;
    int ret;
    void* image_data = NULL;

    if (mouse->hw_cursor_fb == NULL)
    {
        mouse->hw_cursor_fb = malloc(sizeof(mydrm_fb_t));
        memset(mouse->hw_cursor_fb, 0, sizeof(mydrm_fb_t));
    }

    mouse->hw_cursor_fb->width = mouse->size;
    mouse->hw_cursor_fb->height = mouse->size;

    if (!mydrm_create_framebuffer(data->fd, mouse->hw_cursor_fb))
    {
        fprintf(stderr, "Failed to create framebuffer for hardware cursor!\n");
        perror("\tError");
        return -1;
    }

    size_t size = load_image("cursor.data", &image_data);

    if (image_data)
        memcpy(mouse->hw_cursor_fb->pixels, image_data, size);
    else
        memset(mouse->hw_cursor_fb->pixels, 0xFF, mouse->hw_cursor_fb->size);
    free(image_data);

    ret = mydrm_set_cursor(data->fd, data->crt_id, mouse->hw_cursor_fb->handle, mouse->size, mouse->size);

    if (ret == -1)
    {
        perror("Failed to set hardware cursor");
    }    

    ret = mydrm_move_cursor(data->fd, data->crt_id, data->width / 2, data->height / 2);

    if (ret == -1)
        perror("Failed to move hardware cursor");

    return ret;
}
