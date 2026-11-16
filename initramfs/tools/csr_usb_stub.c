/* bccmd (BlueZ 5.55) links its USB transport unconditionally; the TouchPad chip is on a UART, so the
 * USB transport is replaced by stubs that always fail (no libusb needed). */
#include <stdint.h>

int csr_open_usb(char *device) { return -1; }
int csr_read_usb(uint16_t varid, uint8_t *value, uint16_t length) { return -1; }
int csr_write_usb(uint16_t varid, uint8_t *value, uint16_t length) { return -1; }
void csr_close_usb(void) { }
