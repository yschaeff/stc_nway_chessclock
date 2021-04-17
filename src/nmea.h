#include <time.h>

/* Protocol on the wire:
 * 1 byte SYNC
 * 1 byte OPC
 * 1 byte DATA0
 * 1 byte DATA1
 * 1 byte CHECKSUM
*/

#define SYNC_BYTE 0xFA
enum ISR_STATE {
    ISR_STATE_SYNC,
    ISR_STATE_OPC,
    ISR_STATE_DATA0,
    ISR_STATE_DATA1,
    ISR_STATE_CHECKSUM,
};

#define BAUDRATE 9600 // serial port speed (4800/9600 - standard for GPS)

#define MAX_PACKET_SIZE 3

uint8_t rx_buf[MAX_PACKET_SIZE];
enum ISR_STATE isr_rx_state = ISR_STATE_SYNC;
volatile __bit rx_packet_available = 0;

uint8_t tx_buf[MAX_PACKET_SIZE + 1]; //+1 for checksum
enum ISR_STATE isr_tx_state;

void uart1_init()
{
    //P_SW1 |= (1 << 6);          // move UART1 pins -> P3_6:rxd, P3_7:txd
    // UART1 use Timer2
    T2L = (65536 - (FOSC / 4 / BAUDRATE)) & 0xFF;
    T2H = (65536 - (FOSC / 4 / BAUDRATE)) >> 8;
    SM1 = 1;                    // serial mode 1: 8-bit async
    AUXR |= 0x14;               // T2R: run T2, T2x12: T2 clk src sysclk/1
    AUXR |= 0x01;               // S1ST2: T2 is baudrate generator
    ES = 1;                     // enable uart1 interrupt
    EA = 1;                     // enable interrupts
    REN = 1;
}

uint8_t calc_checksum(uint8_t opc, uint8_t data0, uint8_t data1)
{
    uint8_t checksum = SYNC_BYTE;
    checksum += opc;
    checksum += data0;
    checksum += data1;
    return checksum;
}

void uart1_isr() __interrupt 4 __using 2
{
    /* Receive interrupt */
    if (RI) {
        RI = 0;                 // clear int
        /* Read byte from UART */
        uint8_t rx_byte = SBUF;
        switch(isr_rx_state)
        {
            case ISR_STATE_SYNC:
                if (rx_byte == SYNC_BYTE)
                    isr_rx_state++;
                break;

            case ISR_STATE_OPC: rx_buf[0] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA0: rx_buf[1] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA1: rx_buf[2] = rx_byte; isr_rx_state++; break;

            case ISR_STATE_CHECKSUM:
                if(calc_checksum(rx_buf[0], rx_buf[1], rx_buf[2]) != rx_byte) {
                    //PANIC MODE?
                    rx_buf[0] = OPC_PANIC;
                }
                rx_packet_available = 1;
                //Restart statemachine
                isr_rx_state = ISR_STATE_SYNC;
                break;
        }
    }

    /* Transmit interrupt */
    if (TI) {
        TI = 0;
        switch(isr_tx_state)
        {
            case ISR_STATE_SYNC:
                //IDLE!
                break;

            case ISR_STATE_OPC:   SBUF = tx_buf[0]; isr_tx_state++; break;
            case ISR_STATE_DATA0: SBUF = tx_buf[1]; isr_tx_state++; break;
            case ISR_STATE_DATA1: SBUF = tx_buf[2]; isr_tx_state++; break;

            case ISR_STATE_CHECKSUM:
                SBUF = tx_buf[3];
                isr_tx_state = ISR_STATE_SYNC;
                break;
        }
    }
}

void uart1_send_packet(uint8_t opc, uint8_t data0, uint8_t data1)
{
    tx_buf[0] = opc;
    tx_buf[1] = data0;
    tx_buf[2] = data1;
    tx_buf[3] = calc_checksum(tx_buf[0], tx_buf[1], tx_buf[2]);
    /* Start ISR by sending the first byte */
    isr_tx_state = ISR_STATE_OPC;
    SBUF = SYNC_BYTE;
}

