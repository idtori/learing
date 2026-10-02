#ifndef SERIAL_H
#define SERIAL_H
int serial_open(const char *dev);   /* 返回 0 成功 */
void serial_move(const char *dir);  /* dir = "left"/"right"/"up"/"down"/"stop" */
void serial_close(void);
#endif