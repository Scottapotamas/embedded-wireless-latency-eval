/* -------------------------------------------------------------------------- */

typedef enum {
    SPP_SEND_CB,
    SPP_RECV_CB,
} spp_event_id_t;

typedef struct {
    uint32_t bytes_sent;
    bool congested;
} spp_event_send_cb_t;

typedef struct {
    uint8_t *data;
    uint32_t data_len;
} spp_event_recv_cb_t;

typedef union {
    spp_event_send_cb_t send_cb;
    spp_event_recv_cb_t recv_cb;
} spp_event_data_t;

// Main task queue needs to support send and receive events
// The ID field helps distinguish between them
typedef struct {
    spp_event_id_t id;
    spp_event_data_t data;
} spp_event_t;

/* -------------------------------------------------------------------------- */
