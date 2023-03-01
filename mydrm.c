#include "mydrm.h"

int cursor_size = 32;

int mydrm_ioctl(int fd, unsigned long request, void* arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

int mydrm_open(const char* dev_path)
{
    int fd = open(dev_path, O_RDWR | O_CLOEXEC);

    if (fd == -1)
    {
        char errmsg[128] = {0};
        snprintf(errmsg, 128, "open: %s", dev_path);
        perror(errmsg);
        return fd;
    }

    struct drm_get_cap get_cap = {
        .capability = DRM_CAP_DUMB_BUFFER,
        .value = 0
    };

    if (mydrm_ioctl(fd, DRM_IOCTL_GET_CAP, &get_cap) == -1 || !get_cap.value)
        return -EOPNOTSUPP;

    return fd;
}

int mydrm_get_res(int fd, struct drm_mode_card_res* res)
{
    memset(res, 0, sizeof(struct drm_mode_card_res));

    int ior = 0;

    if (mydrm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res))
        return -1;

    if (res->count_fbs)
    {
        res->fb_id_ptr = (uint64_t)malloc(res->count_fbs * sizeof(uint32_t));
        memset((void*)res->fb_id_ptr, 0, res->count_fbs * sizeof(uint32_t));
    }

    if (res->count_crtcs)
    {
        res->crtc_id_ptr = (uint64_t)malloc(res->count_crtcs * sizeof(uint32_t));
        memset((void*)res->crtc_id_ptr, 0, res->count_crtcs * sizeof(uint32_t));
    }

    if (res->count_connectors)
    {
        res->connector_id_ptr = (uint64_t)malloc(res->count_connectors * sizeof(uint32_t));
        memset((void*)res->connector_id_ptr, 0, res->count_connectors * sizeof(uint32_t));
    }

    if (res->count_encoders)
    {
        res->encoder_id_ptr = (uint64_t)malloc(res->count_encoders * sizeof(uint32_t));
        memset((void*)res->encoder_id_ptr, 0, res->count_encoders * sizeof(uint32_t));
    }

    ior = mydrm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res);

    if (ior)
        return -1;
    
    return 0;
}

int mydrm_get_connector(int fd, int id, struct drm_mode_get_connector* conn)
{
    memset(conn, 0, sizeof(struct drm_mode_get_connector));
    conn->connector_id = id;
    conn->count_modes = 0;

    if (mydrm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn))
    {
        perror("ioctl DRM_IOCTL_MODE_GETCONNECTOR (1)");
        return -1;
    }

    if (conn->count_props)
    {
        conn->props_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint32_t));
        memset((void*)conn->props_ptr, 0, conn->count_props * sizeof(uint32_t));
        conn->prop_values_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint64_t));
        memset((void*)conn->prop_values_ptr, 0, conn->count_props * sizeof(uint64_t));
    }

    if (conn->count_modes)
    {
        conn->modes_ptr = (uint64_t)malloc(conn->count_modes * sizeof(struct drm_mode_modeinfo));
        memset((void*)conn->modes_ptr, 0, conn->count_modes * sizeof(struct drm_mode_modeinfo));
    }

    if (conn->count_encoders)
    {
        conn->encoders_ptr = (uint64_t)malloc(conn->count_encoders * sizeof(uint32_t));
        memset((void*)conn->encoders_ptr, 0, conn->count_encoders * sizeof(uint32_t));
    }

    if (mydrm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn))
    {
        perror("ioctl DRM_IOCTL_MODE_GETCONNECTOR (2)");
        return -1;
    }

    return 0;
}

int mydrm_get_encorder(int fd, int id, struct drm_mode_get_encoder* enc)
{
    memset(enc, 0, sizeof(struct drm_mode_get_encoder));
    enc->encoder_id = id;

    if (mydrm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, enc))
    {
        perror("ioctl DRM_IOCTL_MODE_GETENCODER");
        return -1;
    }
    
    return 0;
}

int mydrm_handle_event(int fd, struct mydrm_event_context* ctx)
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

bool mydrm_create_framebuffer(int fd, struct mydrm_buf* buf)
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

void* load_image(const char* path)
{
    void* data = NULL;
    int fd = open(path, O_RDONLY);

    if (fd == -1)
    {
        perror("open");
        return NULL;
    }
    
    struct stat stat;
    if (fstat(fd, &stat) == -1)
    {
        perror("fstat");
        close(fd);
        return NULL;
    }

    data = malloc(stat.st_size);
    memset(data, 0, stat.st_size);

    if (read(fd, data, stat.st_size) == -1)
    {
        perror("read");
        close(fd);
        return NULL;
    }

    close(fd);

    return data;
}

int mydrm_setup_hardware_cursor(struct mydrm_data* data)
{
    struct mydrm_buf* hw_cursor = &data->hw_cursor;
    int ret;

    hw_cursor->width = cursor_size;
    hw_cursor->height = cursor_size;

    if (!mydrm_create_framebuffer(data->fd, hw_cursor))
    {
        fprintf(stderr, "Failed to create framebuffer for hardware cursor!\n");
        perror("\tError");
        return -1;
    }

    void* image_data = load_image("cursor.data");

    if (image_data)
    {
        memcpy(hw_cursor->map, image_data, hw_cursor->size);
        free(image_data);
    }
    else
    {
        memset(hw_cursor->map, 0xFF, hw_cursor->size);
    }

    ret = mydrm_set_cursor(data->fd, data->crt_id, hw_cursor->handle, cursor_size, cursor_size);

    if (ret == -1)
    {
        perror("Failed to set hardware cursor");
    }    

    ret = mydrm_move_cursor(data->fd, data->crt_id, data->width / 2, data->height / 2);

    if (ret == -1)
        perror("Failed to move hardware cursor");

    return ret;
}