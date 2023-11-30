#ifndef RFM95_DEFINES_H
#define RFM95_DEFINES_H

typedef enum {
    RFM9X_LORA_BW_7p8k,            // 0x00
    RFM9X_LORA_BW_10p4k,           // 0x01
    RFM9X_LORA_BW_15p6k,           // 0x02
    RFM9X_LORA_BW_20p8k,           // 0x03
    RFM9X_LORA_BW_31p25k,          // 0x04
    RFM9X_LORA_BW_41p7k,           // 0x05
    RFM9X_LORA_BW_62p5k,           // 0x06
    RFM9X_LORA_BW_125k,            // 0x07
    RFM9X_LORA_BW_250k,            // 0x08
    RFM9X_LORA_BW_500k,            // 0x09
} lora_bw_t;

typedef enum {
    RFM9X_LORA_CR_4_5 = 0x01,      // 0x01
    RFM9X_LORA_CR_4_6,             // 0x02
    RFM9X_LORA_CR_4_7,             // 0x03
    RFM9X_LORA_CR_4_8,             // 0x04
} lora_cr_t;

typedef enum {
    // Currently this driver supports only explicit header mode
    // Setting the spreading factor to 6 is no compatible with
    // explicit header mode... so no support.
    // RFM9X_LORA_SF_64 = 6,       // 0x06
    RFM9X_LORA_SF_128 = 7,         // 0x07
    RFM9X_LORA_SF_256,             // 0x08
    RFM9X_LORA_SF_512,             // 0x09
    RFM9X_LORA_SF_1024,            // 0x0A
    RFM9X_LORA_SF_2048,            // 0x0B
    RFM9X_LORA_SF_4096,            // 0x0C
} lora_sf_t;

// Highest bit (0x80 offset) enables Long Range (LoRA) mode
// Bits 6,5 specify FSK (00) or OOK (01)
// Bottom 3 bits specify the mode
typedef enum {
    RFM9X_MODE_SLEEP = 0x00,      // 0x00

    RFM9X_FSK_MODE_SLEEP = 0x00,  // 0x00
    RFM9X_FSK_MODE_STDBY,         // 0x01
    RFM9X_FSK_MODE_FSTX,          // 0x02  // No support in driver
    RFM9X_FSK_MODE_TX,            // 0x03
    RFM9X_FSK_MODE_FSRX,          // 0x04  // No support in driver
    RFM9X_FSK_MODE_RX_CONTINUOUS, // 0x05
    RFM9X_FSK_MODE_RX,            // 0x06  // No support in driver
    RFM9X_FSK_MODE_CAD,           // 0x07  // No support in driver

    RFM9X_OOK_MODE_SLEEP = 0x20,  // 0x00
    RFM9X_OOK_MODE_STDBY,         // 0x01
    RFM9X_OOK_MODE_FSTX,          // 0x02  // No support in driver
    RFM9X_OOK_MODE_TX,            // 0x03
    RFM9X_OOK_MODE_FSRX,          // 0x04  // No support in driver
    RFM9X_OOK_MODE_RX_CONTINUOUS, // 0x05
    RFM9X_OOK_MODE_RX,            // 0x06  // No support in driver
    RFM9X_OOK_MODE_CAD,           // 0x07  // No support in driver

    RFM9X_LORA_MODE_SLEEP = 0x80,  // 0x00
    RFM9X_LORA_MODE_STDBY,         // 0x01
    RFM9X_LORA_MODE_FSTX,          // 0x02  // No support in driver
    RFM9X_LORA_MODE_TX,            // 0x03
    RFM9X_LORA_MODE_FSRX,          // 0x04  // No support in driver
    RFM9X_LORA_MODE_RX_CONTINUOUS, // 0x05
    RFM9X_LORA_MODE_RX,            // 0x06  // No support in driver
    RFM9X_LORA_MODE_CAD,           // 0x07  // No support in driver
    RFM9X_LORA_MODE_INVALID = 0xFF
} lora_mode_t;

typedef enum {
    RFM95_STATUS_OK = 0x00,
    RFM95_STATUS_ERROR,
} rfm95_status_t;

// See sx1276_77_78_79.pdf page 69 for mapping table
typedef enum {
    // TODO: Most of this functionality isn't implemented...
    //       sorry if you're reading this...
    RFM95_IRQ_TXDONE = 0x00,
    RFM95_IRQ_RXDONE,
    RFM95_IRQ_RXTIMEOUT,
    RFM95_IRQ_MODEREADY,
    RFM95_IRQ_VALIDHEADER,
    RFM95_IRQ_PAYLOADCRCERROR,
    RFM95_IRQ_FHSSCHANGECHANNEL,
    RFM95_IRQ_CLKOUT,
    RFM95_IRQ_PLLLOCK,
    RFM95_IRQ_CADDONE,
    RFM95_IRQ_CADDETECTED,
} rfm95_irq_type;

#endif //RFM95_DEFINES_H