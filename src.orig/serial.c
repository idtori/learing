#include "serial.h"
#include <stdio.h>
int serial_open(const char *dev) 
{
    printf("[serial] open %s (stub)\n", dev); return 0; 
}
void serial_move(const char *dir) 
{
    printf("[serial] move %s (stub)\n", dir); 
}
void serial_close(void) {}