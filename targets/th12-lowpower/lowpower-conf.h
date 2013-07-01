#ifndef LOWPOWER_CONF_H
#define LOWPOWER_CONF_H

#define RPL_CONF_LEAF_ONLY 1
#define RESOLV_CONF_SUPPORTS_MDNS 0
/* DNS retries are in 250ms increments */
/* backoff is retrynum * retrynum * 3 (see check_entries in resolv.c)*/
/* max wait MAX = sqrt (WAIT / (3 * .25)) */ 
/* 8 will be 48 sec */
#define RESOLV_CONF_MAX_RETRIES 8

#define UIP_CONF_DS6_NBR_NBU     4
#define UIP_CONF_DS6_ROUTE_NBU   0

#define RF_CHANNEL 16

#define UART1_CONF_TX_BUFFERSIZE 32
#define UART1_CONF_RX_BUFFERSIZE 32
#define UART2_CONF_TX_BUFFERSIZE 32
#define UART2_CONF_RX_BUFFERSIZE 32

#endif
