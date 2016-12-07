#ifndef UV_MSG_FRAMING_H
#define UV_MSG_FRAMING_H

#include <uv.h>


/* Callback Functions */

typedef void (*uv_free_cb)(uv_handle_t* handle, void* ptr);

typedef void (*uv_msg_read_cb)(uv_stream_t* stream, void *msg, int size);

typedef void (*uv_msg_write_cb)(uv_write_t *req, int status);


/* Functions */

int uv_msg_read_start(uv_stream_t* stream, uv_alloc_cb alloc_cb, uv_msg_read_cb msg_read_cb, uv_free_cb free_cb);

int uv_msg_send(uv_write_t *req, uv_stream_t* stream, void *msg, int size, uv_msg_write_cb msg_write_cb);


/* Message Read Structure */

typedef struct {
    char *buf;
    int alloc_size;
    int filled;
    uv_alloc_cb alloc_cb;
    uv_free_cb free_cb;
    uv_msg_read_cb msg_read_cb;
} msg_buf_t;


/* Message Write Structure */

typedef struct {
    uv_write_t req;   /* this cannot be a pointer */
    uv_write_t *preq; /* maybe this can be removed */
    uv_buf_t buf[2];
    int msg_size;     /* in network order! */
    uv_msg_write_cb msg_write_cb;
} msg_write_req_t;


#endif  // UV_MSG_FRAMING_H
