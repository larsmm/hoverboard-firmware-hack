#include <reent.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include "stm32f1xx_hal.h"

extern volatile uint8_t uart_buf[200];

__attribute__((__used__)) int _write(int fd, const char *ptr, int len){
    while(DMA1_Channel2->CNDTR != 0);
    strncpy((char*)uart_buf,ptr,len);
    DMA1_Channel2->CCR    &= ~DMA_CCR_EN;
    DMA1_Channel2->CNDTR   = len;
    DMA1_Channel2->CMAR    = (uint32_t)uart_buf;
    DMA1_Channel2->CCR    |= DMA_CCR_EN;
    return len;
}
