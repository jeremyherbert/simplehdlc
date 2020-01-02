# simplehdlc
a simple embedded-friendly library inspired by HDLC encoding for transforming a byte-oriented stream transport into a packet transport between two devices. It is suitable for use in resource constrained embedded systems due to its small size and due to requiring only static allocation. It is written in C99. A python implementation is also available at [https://github.com/jeremyherbert/python-simplehdlc](https://github.com/jeremyherbert/python-simplehdlc).

The packet structure is intended to be very simple, and trades customisation for simplicity. The maximum payload size per packet is 65536 bytes. Please note that while the HDLC structure was used as inspiration, this library is not compatible with ISO/IEC 13239:2002 (and is not intended to be).

License is MIT (see LICENSE file for more information). 

### Packet structure

simplehdlc uses a packet structure aligned to an 8 bit boundary as it is intended for use in byte-oriented stream transports. The structure is outlined in the table below.

Description | `0x7E` (frame boundary marker) | payload length | payload | CRC32 
--- | --- | --- | --- | ---
Size (bytes) | 1 | 2 | N | 4 

The payload length is the length of the payload exclusively (no other fields are included). It also does *not include* any escape characters. As such, the number of bytes encoded into the payload field may not match the payload length field while the data is on the wire.

The payload length and CRC32 are both sent MSB first, and the payload is sent with the 0th byte first, and the Nth byte last. The ethernet polynomial is used for the CRC32 (this matches the python `binascii.crc32` function).

Two bytes are reserved in this protocol, the *frame boundary marker* and the *escape marker*. The frame boundary marker indicates the start of a packet, and the escape marker indicates that a reserved byte will be transmitted next. To send a reserved byte, first the escape marker is sent, and then the byte is sent with the 5th bit flipped (`x ^ (1 << 5)`).

By default, the library is configured to use `0x7E` as the frame boundary marker and `0x7D` as the escape marker. It is recommended that you do not change this. 

The parser state-machine is configured to reset if the frame boundary marker appears in any location. Thus, if the parser is fed garbage data and gets stuck waiting to read a very long packet, sending a `0x7E` will cause the pending packet data to be discarded immediately, allowing you to send a new packet. In addition, you can send as many frame boundary markers as you wish during the time when the transport is otherwise idle; they will be ignored by the parser. This may be useful if you need to send data to keep a link alive, or if you need to perform some sort of alignment/padding of data.

### Usage

To use this library for parsing or callback encoding, you must do the following things:

1. Create an instance of the `simplehdlc_callbacks_t` structure, and assign the callbacks as necessary
2. Call `simplehdlc_init` with your pre-allocated buffers and callback structure
3. Call encoding/parsing functions as necessary.

The callback structure definition is as follows:

```c
typedef struct {
    void (*rx_packet_callback)(const uint8_t *payload, uint16_t len, void *user_ptr);
    void (*tx_byte_callback)(uint8_t byte, void *user_ptr);
    void (*tx_flush_buffer_callback)(void *user_ptr);
} simplehdlc_callbacks_t;
```

Depending on which functions you are expecting to call, some of these callbacks will be unused and can be set to `NULL`. If you are not using the encode to callback functionality, both `tx_byte_callback` and `tx_flush_buffer_callback` can be set to `NULL`. If you are not using the parsing functionality, `rx_packet_callback` can be set to `NULL`. All three callbacks allow some opaque user data to be passed to the callback function via `user_ptr`. 

Once the callback structure is appropriately populated, call the `simplehdlc_init` function to initialise a `simplehdlc_context_t` structure:

```c
void simplehdlc_init(simplehdlc_context_t *context, uint8_t *parse_buffer, size_t parse_buffer_len, const simplehdlc_callbacks_t *callbacks, void *user_ptr)
```

where `parse_buffer` is a pointer to a buffer of size `parse_buffer_len`, `callbacks` is a pointer to the callback structure from above, and `user_ptr` is a pointer to some object which will be passed to the callback functions via their corresponding `user_ptr` argument. `parse_buffer` must be large enough to hold the largest packet that you expect to receive, otherwise it will be ignored by the parser.

At this point, you can simply call `simplehdlc_parse` with your context whenever any data arrives over the stream transport.

simplehdlc provides two separate means to encode a payload into a packet, depending on your resource constraints. On severely resource constrained devices or with large payloads, you may not want to store all of the data in memory before sending it. In this case, you can use `simplehdlc_encode_to_callback`, where a callback function is called to send each byte. However, if memory is plentiful, you can encode a payload directly to a buffer using `simplehdlc_encode_to_buffer`. You do not need to create a context structure or run `simplehdlc_init` if using this latter function.

Some examples of usage are shown below, and more are available in `tests/main.c`.

#### Parsing example

```c
static bool decode_success = false;
static size_t decoded_length = 0;

static void decode_success_callback(const uint8_t *payload, uint16_t payload_len, void *user_ptr) {
    // *payload => {1}
    // payload_len => 1 
    // user_ptr => NULL
}

void parse_example() {
    decode_success = false;
    decoded_length = 0;

    uint8_t payload[] = {1};
    uint8_t encoded[] = {0x7E, 0x00, 0x01, 0x01, 0xA5, 0x05, 0xDF, 0x1B};

    uint8_t rx_buffer[512];
    simplehdlc_context_t context;

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    simplehdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *) payload);
    simplehdlc_parse(&context, encoded, sizeof(encoded));
}
```

#### Encode to buffer example

```c
void encode_to_buffer_example() {
    uint8_t buffer[8];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[1] = {1};

    assert(simplehdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == SIMPLEHDLC_OK);
}
    
```


#### Encode to callback example

```c
static void tx_callback(uint8_t byte, void *user_ptr) {
    // transmit byte over UART, add byte to a transmit queue, etc
}

static void tx_flush_callback(void *user_ptr) {
    // flush TX queue, etc
}

void encode_to_callback_example() {
    uint8_t payload[2] = {0x7E, 0x7D};

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.tx_byte_callback = tx_callback;
    callbacks.tx_flush_buffer_callback = tx_flush_callback;
    simplehdlc_context_t context;

    // no parsing is being used, so we can pass NULL as the pointer to the parse buffer
    simplehdlc_init(&context, NULL, 0, &callbacks, NULL);

    assert(simplehdlc_encode_to_callback(&context, payload, sizeof(payload), true) == SIMPLEHDLC_OK);
}
```

### Building

To use this library, add `simplehdlc.c` and `simplehdlc_crc32.c` to your build, and add the corresponding header files to your include path. 

The CRC implementation uses a hard-coded 256 byte lookup table, as flash memory is generally more abundant than RAM in embedded systems. If you are really struggling with flash size in your application, this can be replaced with a just-in-time computed version.