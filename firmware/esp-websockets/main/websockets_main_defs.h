#ifndef TCP_DEFS_H
#define TCP_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

typedef enum {
    BENCH_SEND_CB,
    BENCH_RECV_CB,
} bench_event_id_t;

typedef struct {
    uint32_t bytes_sent;
} bench_event_send_cb_t;

typedef struct {
    uint8_t *data;
    uint32_t data_len;
} bench_event_recv_cb_t;

typedef union {
    bench_event_send_cb_t send_cb;
    bench_event_recv_cb_t recv_cb;
} bench_event_data_t;

// Main task queue needs to support send and receive events
// The ID field helps distinguish between them
typedef struct {
    bench_event_id_t id;
    bench_event_data_t data;
} bench_event_t;

#define BENCHMARK_QUEUE_SIZE (8)

#define BENCH_DATA_MAX_LEN (2048)

/* -------------------------------------------------------------------------- */

#define NO_DATA_TIMEOUT_SEC (15)

#define CONFIG_WEBSOCKET_SERVER_URI "ws://192.168.1.20/ws"

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif  // end TCP_DEFS_H