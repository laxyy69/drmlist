#include "mydrm.h"

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
        perror("open");
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

    if (mydrm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn))
    {
        perror("ioctl DRM_IOCTL_MODE_GETCONNECTOR (1)");
        return -1;
    }
    
    if (conn->count_props)
    {
        conn->props_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint32_t));
        conn->prop_values_ptr = (uint64_t)malloc(conn->count_props * sizeof(uint32_t));
    }

    if (conn->count_modes)
    {
        conn->modes_ptr = (uint64_t)malloc(conn->count_modes * sizeof(struct drm_mode_modeinfo));
    }

    if (conn->count_encoders)
    {
        conn->encoders_ptr = (uint64_t)malloc(conn->count_encoders * sizeof(uint32_t));
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
        }
    }

    return 0;
}