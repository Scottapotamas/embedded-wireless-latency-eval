#ifndef RFM95_PRIVATE_DEFINES_H
#define RFM95_PRIVATE_DEFINES_H

#define RFM95_VERSION (0x12)

enum {
    PWL_RFM9X_INTERRUPT_STATUS_MASK  = 0b01101000,
    PWL_RFM9X_RX_DONE_INTERRUPT_FLAG = 0b01000000,
    PWL_RFM9X_RX_CRC_ERROR_FLAG      = 0b00100000,
    PWL_RFM9X_TX_INTERRUPT_MASK      = 0b00001000
};

enum {
    RFM9X_LORA_BW_BitPos = 0x04,
    RFM9X_LORA_CR_BitPos = 0x01,
    RFM9X_LORA_SF_BitPos = 0x04,
};

enum {
    RFM9X_REG_Fifo                = 0x00,
    RFM9X_REG_OpMode              = 0x01,
    RFM9X_REG_FrfMsb              = 0x06,
    RFM9X_REG_FrfMid              = 0x07,
    RFM9X_REG_FrfLsb              = 0x08,
    RFM9X_REG_PaConfig            = 0x09,
    RFM9X_REG_PaRamp              = 0x0A,
    RFM9X_REG_Ocp                 = 0x0B,
    RFM9X_REG_Lna                 = 0x0C,
    RFM9X_REG_FifoAddrPtr         = 0x0D,
    RFM9X_REG_FifoTxBaseAddr      = 0x0E,
    RFM9X_REG_FifoRxBaseAddr      = 0x0F,
    RFM9X_REG_FifoRxCurrentAddr   = 0x10,
    RFM9X_REG_IrqFlagsMask        = 0x11,
    RFM9X_REG_IrqFlags            = 0x12,
    RFM9X_REG_RxNbBytes           = 0x13,
    RFM9X_REG_RxHeaderCntValueMsb = 0x14,
    RFM9X_REG_RxHeaderCntValueLsb = 0x15,
    RFM9X_REG_RxPacketCntValueMsb = 0x16,
    RFM9X_REG_RxPacketCntValueLsb = 0x17,
    RFM9X_REG_ModemStat           = 0x18,
    RFM9X_REG_PktSnrValue         = 0x19,
    RFM9X_REG_PktRssiValue        = 0x1A,
    RFM9X_REG_RssiValue           = 0x1B,
    RFM9X_REG_HopChannel          = 0x1C,
    RFM9X_REG_ModemConfig1        = 0x1D,
    RFM9X_REG_ModemConfig2        = 0x1E,
    RFM9X_REG_SymbTimeoutLsb      = 0x1F,
    RFM9X_REG_PreambleMsb         = 0x20,
    RFM9X_REG_PreambleLsb         = 0x21,
    RFM9X_REG_PayloadLength       = 0x22,
    RFM9X_REG_MaxPayloadLength    = 0x23,
    RFM9X_REG_HopPeriod           = 0x24,
    RFM9X_REG_FifoRxByteAddr      = 0x25,
    RFM9X_REG_ModemConfig3        = 0x26,
    RFM9X_REG_PpmCorrection       = 0x27,
    RFM9X_REG_FeiMsb              = 0x28,
    RFM9X_REG_FeiMid              = 0x29,
    RFM9X_REG_FeiLsb              = 0x2A,
    RFM9X_REG_RssiWideband        = 0x2C,
    RFM9X_REG_IfFreq1             = 0x2F,
    RFM9X_REG_IfFreq2             = 0x30,
    RFM9X_REG_DetectOptimize      = 0x31,
    RFM9X_REG_InvertIQ            = 0x33,
    RFM9X_REG_HighBwOptimize1     = 0x36,
    RFM9X_REG_DetectionThreshold  = 0x37,
    RFM9X_REG_SyncWord            = 0x39,
    RFM9X_REG_HighBwOptimize2     = 0x3A,
    RFM9X_REG_InvertIQ2           = 0x3B,
    RFM9X_REG_DioMapping1         = 0x40,
    RFM9X_REG_DioMapping2         = 0x41,
    RFM9X_REG_Version             = 0x42,
    RFM9X_REG_Tcxo                = 0x4B,
    RFM9X_REG_PaDac               = 0x4D,
    RFM9X_REG_FormerTemp          = 0x5B,
    RFM9X_REG_AgcRef              = 0x61,
    RFM9X_REG_AgcThresh1          = 0x62,
    RFM9X_REG_AgcThresh2          = 0x63,
    RFM9X_REG_AgcThresh3          = 0x64,
    RFM9X_REG_Pll                 = 0x70,
    RFM9X_REG_PA_DAC_LOW_POWER    = 0x84,
    RFM9X_REG_PA_DAC_HIGH_POWER   = 0x87,
};

typedef struct
{
    union {
        struct {
            uint8_t output_power : 4;
            uint8_t max_power : 3;
            uint8_t pa_select : 1;
        };
        uint8_t buffer;
    };
} rfm95_pa_config_byte_t;

#endif //RFM95_PRIVATE_DEFINES_H
