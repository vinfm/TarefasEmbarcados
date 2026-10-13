#include <stdbool.h>
#include <stdio.h>
#include "usb_serial.h"

int main(void) {
    usb_serial_init();

    while (1) {

        printf("\nExemplo uso.\n");
        fflush(stdout);
    }
}