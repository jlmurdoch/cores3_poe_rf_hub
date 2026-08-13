#ifndef SX1276_868_FSK_PKT_H
#define SX1276_868_FSK_PKT_H

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"
#include "sx127x.h"
#include "config.h"

static TaskHandle_t sx1276_868_task_handle = NULL;

void init_sx1276_868_fsk_pkt(void);
#endif