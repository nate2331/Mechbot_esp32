#pragma once
enum hardwareSerial_error_t {
  UART_NO_ERROR = 0, UART_BREAK_ERROR = 1, UART_BUFFER_FULL_ERROR = 2,
  UART_FIFO_OVF_ERROR = 3, UART_FRAME_ERROR = 4, UART_PARITY_ERROR = 5
};
inline int fakeUartRx[3] = {-1, -1, -1};
inline int fakeUartTx[3] = {-1, -1, -1};
inline int uart_get_RxPin(unsigned port) { return fakeUartRx[port]; }
inline int uart_get_TxPin(unsigned port) { return fakeUartTx[port]; }
