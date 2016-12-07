# libuv_message_framing

Message Framing for libuv


## *** DRAFT ***

This code is experimental and the interface can be modified


## Example

### Sending Messages
```
uv_msg_send((uv_write_t*)req, (uv_stream_t*) stream, msg, size, msg_write_cb);
```

### Receiving Messages
```
uv_msg_read_start((uv_stream_t*) socket, alloc_cb, msg_read_cb, free_cb);
```

Check [example.c](example.c)


## Compiling

### On Linux

gcc example.c -luv

### On Windows

gcc example.c -llibuv -lws2_32


## TO DO

 * close the socket/stream releasing the allocated message read structure
 * (maybe) use `uv_buf_t bufs[]` instead of `void *msg` on uv_msg_send()
 * check if the function names are correct and compatible with libuv
 * unit tests
 
