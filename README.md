# libuv_message_framing

[![Build Status](https://travis-ci.org/litesync/libuv_message_framing.svg?branch=master)](https://travis-ci.org/litesync/libuv_message_framing)

Message-based communication for libuv

This code implements length-prefixed message framing on top of streams.

It is expected to work with TCP, Unix domain sockets (Linux) and Named Pipes (Windows).


## Usage

### Stream Initialization for TCP

```C
uv_msg_t* socket = malloc(sizeof(uv_msg_t));
uv_msg_init(loop, socket, UV_TCP);
```

### Stream Initialization for Unix domain sockets or Named Pipes 

```C
uv_msg_t* socket = malloc(sizeof(uv_msg_t));
uv_msg_init(loop, socket, UV_NAMED_PIPE);
```

### Sending Messages

```C
uv_msg_send((uv_msg_write_t*)req, (uv_msg_t*) socket, msg, size, write_cb);
```

### Receiving Messages

```C
uv_msg_read_start((uv_msg_t*) socket, alloc_cb, msg_read_cb, free_cb);
```


## Examples

By default libuv does not handle memory management for requests. The above functions
were implemented using the same principle so you are free to use the memory management
you want with them.

But for ease of use and understanding we included 2 examples that use the system 
malloc/free functions.

The message reading implementation is the same.

Both examples also have a user function called `send_message`, but implemented in
a different way.

### Example 1

The [example.c](example.c) has a basic `send_message` function that only accepts 
dynamically allocated messages. The callback must always be supplied and it must
release the memory used for the message and for the request.

```C
send_message(socket, msg, size, on_msg_sent);
```

### Example 2

The [example2.c](example2.c) has a more elaborated `send_message` function that
accepts both static, transient and dynamic messages. This concept is
inherited from [SQLite](https://www.sqlite.org/c3ref/c_static.html).

```C
send_message(socket, msg, size, free_fn, on_msg_sent, user_data);
```

The `free_fn` argument can accept these 3 values:

 * UV_MSG_STATIC

   Use when the message pointer you pass to the function will be valid until after
   the message is sent.
   
 * UV_MSG_TRANSIENT
 
   Use when the message memory will be discarded soon, probably before the message
   is sent. The function will make a copy of the message. Remember that the 
   message sending is asynchronous.

 * A pointer to the destructor or free function that must be automatically called
   to release the memory associated with the message upon the complete delivery or
   failure.

The callback and user data arguments are optional. Examples:

Sending a static message with no callback:

```C
msg = "Hello Mom!";
send_message(socket, msg, strlen(msg)+1, UV_MSG_STATIC, 0, 0);
```

Sending a dynamically allocated message with a notification callback function:

```C
msg = strdup("Hello Dad!");
send_message(socket, msg, strlen(msg)+1, free, on_msg_sent, extra_data);
```


## Compiling

### On Linux

gcc example.c -luv

### On Windows

gcc example.c -llibuv -lws2_32


## Testing

### On Linux

    cd test
    gcc test.c -o test -luv
    LD_LIBRARY_PATH=/usr/local/lib ./test
    
    # or with valgrind:
    LD_LIBRARY_PATH=/usr/local/lib valgrind --leak-check=full --show-reachable=yes ./test

### On Windows

    cd test
    gcc test.c -o test -llibuv -lws2_32
    test


## TO DO

 * (maybe) use `uv_buf_t bufs[]` instead of `void *msg` on uv_msg_send()

