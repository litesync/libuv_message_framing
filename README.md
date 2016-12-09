# libuv_message_framing

Message-based communication for libuv.

This code implements length-prefixed message framing on top of streams.

It is expected to work with TCP, Unix domain sockets (Linux) and Named Pipes (Windows).


## *** DRAFT ***

This code is experimental and the interface can be modified

There is another implementation on the branch 'basic' that can be used on any stream without the initialization bellow but does not deal with handle closing


## Usage

### Stream Initialization for TCP

    uv_msg_t* socket = malloc(sizeof(uv_msg_t));
    uv_msg_init(loop, socket, UV_TCP);

### Stream Initialization for Unix domain sockets or Named Pipes 

    uv_msg_t* socket = malloc(sizeof(uv_msg_t));
    uv_msg_init(loop, socket, UV_NAMED_PIPE);

### Sending Messages

    uv_msg_send((uv_msg_write_t*)req, (uv_stream_t*) stream, msg, size, write_cb);

### Receiving Messages

    uv_msg_read_start((uv_msg_t*) socket, alloc_cb, msg_read_cb, free_cb);

Check [example.c](example.c)


## Compiling

### On Linux

gcc example.c -luv

### On Windows

gcc example.c -llibuv -lws2_32


## Testing

### On Linux

cd test
gcc test.c -o test -luv
test

### On Windows

cd test
gcc test.c -o test -llibuv -lws2_32
test


## TO DO

 * ~~close the socket/stream releasing the allocated message read structure~~
 * (maybe) use `uv_buf_t bufs[]` instead of `void *msg` on uv_msg_send()
 * check if the function names are correct and compatible with libuv
 * ~~unit tests~~
 
