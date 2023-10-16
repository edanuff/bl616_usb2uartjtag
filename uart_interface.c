/**
 * @file uart_interface.c
 * @brief
 *
 * Copyright (c) 2021 Sipeed team
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#include "uart_interface.h"

#include "bflb_mtimer.h"
#include "bflb_dma.h"
#include "bflb_uart.h"
#include "io_cfg.h"
#include "board.h"

#define USB_OUT_RINGBUFFER_SIZE (8 * 1024)
#define UART_RX_RINGBUFFER_SIZE (8 * 1024)
#define UART_TX_DMA_SIZE (4095)

uint8_t usb_rx_mem[USB_OUT_RINGBUFFER_SIZE]
    __attribute__((section(".system_ram")));
uint8_t uart_rx_mem[UART_RX_RINGBUFFER_SIZE]
    __attribute__((section(".system_ram")));

uint8_t src_buffer[UART_TX_DMA_SIZE] __attribute__((section(".tcm_code")));

struct bflb_device_s *uart1;
struct device *dma_ch2;

Ring_Buffer_Type usb_rx_rb;
Ring_Buffer_Type uart1_rx_rb;

void uart_irq_callback(struct device *dev, void *args, uint32_t size,
                       uint32_t state) {
  if (state == UART_INTSTS_RX_FIFO) {
    if (size && size < Ring_Buffer_Get_Empty_Length(&uart1_rx_rb)) {
      Ring_Buffer_Write(&uart1_rx_rb, (uint8_t *)args, size);
    } else {
      printf("RF\r\n");
    }
  } else if (state == UART_INTSTS_RTO) {
    if (size && size < Ring_Buffer_Get_Empty_Length(&uart1_rx_rb)) {
      Ring_Buffer_Write(&uart1_rx_rb, (uint8_t *)args, size);
    } else {
      printf("RTO\r\n");
    }
  } else if (state == UART_INTSTS_RX_END) {
    printf("ov\r\n");
  }
}
void uart1_init(void) {
  uart1 = bflb_device_get_by_name(DEFAULT_TEST_UART);

  if (uart1) {
    device_open(
        uart1, DEVICE_OFLAG_DMA_TX | DEVICE_OFLAG_INT_RX);  // uart0 tx dma mode
    device_control(uart1, DEVICE_CTRL_SUSPEND, NULL);
    device_set_callback(uart1, uart_irq_callback);
    device_control(uart1, DEVICE_CTRL_SET_INT,
                   (void *)(UART_RX_FIFO_IT | UART_RTO_IT));
  }

  dma_register(DMA0_CH2_INDEX, "ch2");
  dma_ch2 = device_find("ch2");
  if (dma_ch2) {
    device_open(dma_ch2, 0);
    // device_set_callback(dma_ch2, NULL);
    // device_control(dma_ch2, DEVICE_CTRL_SET_INT, NULL);
  }
  // device_control(uart1, DEVICE_CTRL_ATTACH_TX_DMA, dma_ch2);
}

void uart1_config(uint32_t baudrate, uint8_t databits,
                  uint8_t parity, uint8_t stopbits) {
  uart_param_cfg_t cfg;
  cfg.baudrate = baudrate;
  cfg.stopbits = stopbits;
  cfg.parity = parity;

  if (databits == 5) {
    cfg.databits = UART_DATA_LEN_5;
  } else if (databits == 6) {
    cfg.databits = UART_DATA_LEN_6;
  } else if (databits == 7) {
    cfg.databits = UART_DATA_LEN_7;
  } else if (databits == 8) {
    cfg.databits = UART_DATA_LEN_8;
  }

  device_control(uart1, DEVICE_CTRL_CONFIG, &cfg);
}

static uint8_t uart1_dtr;
static uint8_t uart1_rts;

void uart1_set_dtr_rts(uint8_t dtr, uint8_t rts) {
  uart1_dtr = dtr;
  uart1_rts = rts;
}

void uart1_dtr_init(void) { gpio_set_mode(uart1_dtr, GPIO_OUTPUT_MODE); }
void uart1_rts_init(void) { gpio_set_mode(uart1_rts, GPIO_OUTPUT_MODE); }
void uart1_dtr_deinit(void) { gpio_set_mode(uart1_dtr, GPIO_INPUT_MODE); }
void uart1_rts_deinit(void) { gpio_set_mode(uart1_rts, GPIO_INPUT_MODE); }
void dtr_pin_set(uint8_t status) { gpio_write(uart1_dtr, status); }
void rts_pin_set(uint8_t status) { gpio_write(uart1_rts, status); }
void ringbuffer_lock() { cpu_global_irq_disable(); }
void ringbuffer_unlock() { cpu_global_irq_enable(); }

void uart_ringbuffer_init(void) {
  /* init mem for ring_buffer */
  memset(usb_rx_mem, 0, USB_OUT_RINGBUFFER_SIZE);
  memset(uart_rx_mem, 0, UART_RX_RINGBUFFER_SIZE);

  /* init ring_buffer */
  Ring_Buffer_Init(&usb_rx_rb, usb_rx_mem, USB_OUT_RINGBUFFER_SIZE,
                   ringbuffer_lock, ringbuffer_unlock);
  Ring_Buffer_Init(&uart1_rx_rb, uart_rx_mem, UART_RX_RINGBUFFER_SIZE,
                   ringbuffer_lock, ringbuffer_unlock);
}

static dma_control_data_t uart_dma_ctrl_cfg = {
    .bits.fix_cnt = 0,
    .bits.dst_min_mode = 0,
    .bits.dst_add_mode = 0,
    .bits.SI = 1,
    .bits.DI = 0,
    .bits.SWidth = DMA_TRANSFER_WIDTH_8BIT,
    .bits.DWidth = DMA_TRANSFER_WIDTH_8BIT,
    .bits.SBSize = 0,
    .bits.DBSize = 0,
    .bits.I = 0,
    .bits.TransferSize = 4095};
static dma_lli_ctrl_t uart_lli_list = {.src_addr = (uint32_t)src_buffer,
                                       .dst_addr = DMA_ADDR_UART1_TDR,
                                       .nextlli = 0};

extern void led_toggle(uint8_t idx);
void uart_send_from_ringbuffer(void) {
  if (Ring_Buffer_Get_Length(&usb_rx_rb)) {
    if (!device_control(dma_ch2, DEVICE_CTRL_DMA_CHANNEL_GET_STATUS, NULL)) {
      uint32_t avalibleCnt =
          Ring_Buffer_Read(&usb_rx_rb, src_buffer, UART_TX_DMA_SIZE);

      if (avalibleCnt) {
        dma_channel_stop(dma_ch2);
        uart_dma_ctrl_cfg.bits.TransferSize = avalibleCnt;
        memcpy(&uart_lli_list.cfg, &uart_dma_ctrl_cfg,
               sizeof(dma_control_data_t));
        device_control(dma_ch2, DEVICE_CTRL_DMA_CHANNEL_UPDATE,
                       (void *)((uint32_t)&uart_lli_list));
        dma_channel_start(dma_ch2);
        led_toggle(0);  // TX indication
      }
    }
  }
}
