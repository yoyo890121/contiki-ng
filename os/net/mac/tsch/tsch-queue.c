/*
 * Copyright (c) 2014, SICS Swedish ICT.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Per-neighbor packet queues for TSCH MAC.
 *         The list of neighbors uses the TSCH lock, but per-neighbor packet array are lock-free.
 *				 Read-only operation on neighbor and packets are allowed from interrupts and outside of them.
 *				 *Other operations are allowed outside of interrupt only.*
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 *         Beshr Al Nahas <beshr@sics.se>
 *         Domenico De Guglielmo <d.deguglielmo@iet.unipi.it >
 */

/**
 * \addtogroup tsch
 * @{
*/

#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/queuebuf.h"
#include "net/mac/tsch/tsch.h"
#include <string.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "TSCH Queue"
#define LOG_LEVEL LOG_LEVEL_MAC

/* Check if TSCH_QUEUE_NUM_PER_NEIGHBOR is power of two */
#if (TSCH_QUEUE_NUM_PER_NEIGHBOR & (TSCH_QUEUE_NUM_PER_NEIGHBOR - 1)) != 0
#error TSCH_QUEUE_NUM_PER_NEIGHBOR must be power of two
#endif

/* We have as many packets are there are queuebuf in the system */
MEMB(packet_memb, struct tsch_packet, QUEUEBUF_NUM);
MEMB(neighbor_memb, struct tsch_neighbor, TSCH_QUEUE_MAX_NEIGHBOR_QUEUES);
LIST(neighbor_list);

/* Testing for QoS swap function.*/
static int8_t data_tcflow;

#define DEBUG DEBUG_PRINT
#include "net/net-debug.h"

/* Broadcast and EB virtual neighbors */
struct tsch_neighbor *n_broadcast;
struct tsch_neighbor *n_eb;

/*---------------------------------------------------------------------------*/
/* Add a TSCH neighbor */
struct tsch_neighbor *
tsch_queue_add_nbr(const linkaddr_t *addr)
{
  struct tsch_neighbor *n = NULL;
  /* If we have an entry for this neighbor already, we simply update it */
  n = tsch_queue_get_nbr(addr);
  if(n == NULL) {
    if(tsch_get_lock()) {
      /* Allocate a neighbor */
      n = memb_alloc(&neighbor_memb);
      if(n != NULL) {
        /* Initialize neighbor entry */
        memset(n, 0, sizeof(struct tsch_neighbor));
        ringbufindex_init(&n->tx_ringbuf, TSCH_QUEUE_NUM_PER_NEIGHBOR);
        linkaddr_copy(&n->addr, addr);
        n->is_broadcast = linkaddr_cmp(addr, &tsch_eb_address)
          || linkaddr_cmp(addr, &tsch_broadcast_address);
        tsch_queue_backoff_reset(n);
        /* Add neighbor to the list */
        list_add(neighbor_list, n);
      }
      tsch_release_lock();
    }
  }
  return n;
}
/*---------------------------------------------------------------------------*/
/* Get a TSCH neighbor */
struct tsch_neighbor *
tsch_queue_get_nbr(const linkaddr_t *addr)
{
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      if(linkaddr_cmp(&n->addr, addr)) {
        return n;
      }
      n = list_item_next(n);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Get a TSCH time source (we currently assume there is only one) */
struct tsch_neighbor *
tsch_queue_get_time_source(void)
{
  if(!tsch_is_locked()) {
    struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
    while(curr_nbr != NULL) {
      if(curr_nbr->is_time_source) {
        return curr_nbr;
      }
      curr_nbr = list_item_next(curr_nbr);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Update TSCH time source */
int
tsch_queue_update_time_source(const linkaddr_t *new_addr)
{
  if(!tsch_is_locked()) {
    if(!tsch_is_coordinator) {
      struct tsch_neighbor *old_time_src = tsch_queue_get_time_source();
      struct tsch_neighbor *new_time_src = NULL;

      if(new_addr != NULL) {
        /* Get/add neighbor, return 0 in case of failure */
        new_time_src = tsch_queue_add_nbr(new_addr);
        if(new_time_src == NULL) {
          return 0;
        }
      }

      if(new_time_src != old_time_src) {
        LOG_INFO("update time source: ");
        LOG_INFO_LLADDR(old_time_src ? &old_time_src->addr : NULL);
        LOG_INFO_(" -> ");
        LOG_INFO_LLADDR(new_time_src ? &new_time_src->addr : NULL);
        LOG_INFO_("\n");

        /* Update time source */
        if(new_time_src != NULL) {
          new_time_src->is_time_source = 1;
          /* (Re)set keep-alive timeout */
          tsch_set_ka_timeout(TSCH_KEEPALIVE_TIMEOUT);
        } else {
          /* Stop sending keepalives */
          tsch_set_ka_timeout(0);
        }

        if(old_time_src != NULL) {
          old_time_src->is_time_source = 0;
        }

#ifdef TSCH_CALLBACK_NEW_TIME_SOURCE
        TSCH_CALLBACK_NEW_TIME_SOURCE(old_time_src, new_time_src);
        #include "sf-callback.h"
        if(old_time_src != NULL) {
          printf("Switching Parent\n");
          sf_simple_switching_parent_callback(&old_time_src->addr, &new_time_src->addr);
        }        
#endif
      }

      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Flush a neighbor queue */
static void
tsch_queue_flush_nbr_queue(struct tsch_neighbor *n)
{
  while(!tsch_queue_is_empty(n)) {
    struct tsch_packet *p = tsch_queue_remove_packet_from_queue(n);
    if(p != NULL) {
      /* Set return status for packet_sent callback */
      p->ret = MAC_TX_ERR;
      LOG_WARN("! flushing packet\n");
      /* Call packet_sent callback */
      mac_call_sent_callback(p->sent, p->ptr, p->ret, p->transmissions);
      /* Free packet queuebuf */
      tsch_queue_free_packet(p);
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Remove TSCH neighbor queue */
static void
tsch_queue_remove_nbr(struct tsch_neighbor *n)
{
  if(n != NULL) {
    if(tsch_get_lock()) {

      /* Remove neighbor from list */
      list_remove(neighbor_list, n);

      tsch_release_lock();

      /* Flush queue */
      tsch_queue_flush_nbr_queue(n);

      /* Free neighbor */
      memb_free(&neighbor_memb, n);
    }
  }
}
/*---------------------------------------------------------------------------*/
/* direct access into the buffer */
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

/* Add packet to neighbor queue. Use same lockfree implementation as ringbuf.c (put is atomic) */
struct tsch_packet *
tsch_queue_add_packet(const linkaddr_t *addr, uint8_t max_transmissions,
                      mac_callback_t sent, void *ptr)
{
  struct tsch_neighbor *n = NULL;
  int16_t put_index = -1;
  struct tsch_packet *p = NULL;
  data_tcflow = -1; /* by default. */
  
  if(!tsch_is_locked()) {
    n = tsch_queue_add_nbr(addr);
    if(n != NULL) {
      put_index = ringbufindex_peek_put(&n->tx_ringbuf);
      if(put_index != -1) {
        p = memb_alloc(&packet_memb);
        if(p != NULL) {
          /* Enqueue packet */
#ifdef TSCH_CALLBACK_PACKET_READY
          TSCH_CALLBACK_PACKET_READY();
#endif
          p->qb = queuebuf_new_from_packetbuf();
          if(p->qb != NULL) {
            p->sent = sent;
            p->ptr = ptr;
            p->ret = MAC_TX_DEFERRED;
            p->transmissions = 0;
            p->max_transmissions = max_transmissions;

            /* show queuebuf information. */
            uint8_t dataLen = queuebuf_datalen(p->qb);
            // for(uint8_t i = 0; i < dataLen; i++) {
            //   uint8_t data = ((uint8_t *)queuebuf_dataptr(p->qb))[i];
            //   PRINTF("%02x ", data);
            // }
            // PRINTF("\n");

            /* check coap have created packet, if will, print it. */
            if(dataLen >= 100 && ((uint8_t *)queuebuf_dataptr(p->qb))[0] == 0x21 &&
               ((uint8_t *)queuebuf_dataptr(p->qb))[dataLen - 4] == 0xf0 &&
               ((uint8_t *)queuebuf_dataptr(p->qb))[dataLen - 3] == 0xff) {
              data_tcflow = ((uint8_t *)queuebuf_dataptr(p->qb))[24]; /* 24 is tcflow location in queuebuf. */
              PRINTF("Traffic classes In TSCH queue : %02x\n", data_tcflow);
              PRINTF("UIP_IP_BUF->tcflow=%d\n", UIP_IP_BUF->tcflow);
            }
            

#if ENABLE_QOS_WHITE
            tsch_queue_resorting_ringbuf_priority(n, p);
#else
            /* Add to ringbuf (actual add committed through atomic operation) */
            n->tx_array[put_index] = p;
            ringbufindex_put(&n->tx_ringbuf);
            LOG_DBG("packet is added put_index %u, packet %p\n",
                   put_index, p);
#endif /* ENABLE_QOS_WHITE */
            return p;
          } else {
            memb_free(&packet_memb, p);
          }
        }
      }
    }
  }
  LOG_ERR("! add packet failed: %u %p %d %p %p\n", tsch_is_locked(), n, put_index, p, p ? p->qb : NULL);
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Resorting ringbuf packet by priority */
void
tsch_queue_resorting_ringbuf_priority(struct tsch_neighbor *n, struct tsch_packet *p)
{
  int16_t put_index = ringbufindex_peek_put(&n->tx_ringbuf); /* peek put ringbuf data. */
  uint8_t ringbufindex_ELM = ringbufindex_elements(&n->tx_ringbuf);

  PRINTF("Data Traffice class value : %02x , %d , Rinbuffer Index Elements : %d .\n", data_tcflow, data_tcflow, ringbufindex_ELM);
  /* if (data_tcflow != -1 && ringbufindex_ELM > 0) */
  if(ringbufindex_ELM > 0) {
    PRINTF(" HELLO I'M IN FUNCTION. \n");
    pkt_priority_sorting(n, p);
  } else {
    PRINTF(" ringbufindex_ELM empty & place \n");
    n->tx_array[put_index] = p;
    ringbufindex_put(&n->tx_ringbuf); /* input ringbuf. */
  }
}

void
pkt_priority_sorting(struct tsch_neighbor *n, struct tsch_packet *p)
{

  uint8_t ringbufSize = ringbufindex_size(&n->tx_ringbuf); /* %16 for loop ring. */
  int8_t ringbufindex_ELM = ringbufindex_elements(&n->tx_ringbuf);
  int16_t put_index = ringbufindex_peek_put(&n->tx_ringbuf); /* peek put ringbuf data. */
  uint8_t current_packet_tcflow = ((uint8_t *)queuebuf_dataptr(p->qb))[24];

  int16_t i = put_index;
  PRINTF("Start the put_index : %d \n", i);

  while(1) {
    int8_t previous_index;

    if(i < 0) {
      i = (ringbufSize - 1);        /* fix the i < 0 , will crash; */
    }
    PRINTF("put_index : %d\n", i);
    PRINTF("left_Pkt_To_Scan: % d\n", ringbufindex_ELM);
    if(ringbufindex_ELM == 0) {
      break;
    }

    if(i == 0) {
      previous_index = (ringbufSize - 1);
    } else {
      previous_index = i - 1;
    }

    struct tsch_packet *temp_p_p = (n->tx_array[previous_index]); /* previous packet to temp_p. */
    uint8_t previous_packet_tcflow = ((uint8_t *)queuebuf_dataptr(temp_p_p->qb))[24];

    /* if the position[24] of packet in is not 0~2(which means it might not be COAP packet) regard them with priority=0 */
    if(current_packet_tcflow < 0 || current_packet_tcflow > 2) {
      current_packet_tcflow = 0;
    }
    if(previous_packet_tcflow < 0 || previous_packet_tcflow > 2) {
      previous_packet_tcflow = 0;
    }
    PRINTF("tcflow_current : %d   tcflow_previous: %d \n", current_packet_tcflow, previous_packet_tcflow);
    if(current_packet_tcflow <= previous_packet_tcflow) {
      break;
    }
    n->tx_array[i] = n->tx_array[previous_index];
    i = i - 1; /* put_index */
    ringbufindex_ELM = ringbufindex_ELM - 1; /* left_Pkt_To_Scan */
  }
  n->tx_array[i] = p;
  PRINTF("End the put_index : %d\n", i);
  ringbufindex_put(&n->tx_ringbuf); /* input ringbuf. */
}
/*---------------------------------------------------------------------------*/
/* Returns the number of packets currently in any TSCH queue */
int
tsch_queue_global_packet_count(void)
{
  return QUEUEBUF_NUM - memb_numfree(&packet_memb);
}
/*---------------------------------------------------------------------------*/
/* Returns the number of packets currently in the queue */
int
tsch_queue_packet_count(const linkaddr_t *addr)
{
  struct tsch_neighbor *n = NULL;
  if(!tsch_is_locked()) {
    n = tsch_queue_add_nbr(addr);
    if(n != NULL) {
      return ringbufindex_elements(&n->tx_ringbuf);
    }
  }
  return -1;
}
/*---------------------------------------------------------------------------*/
/* Remove first packet from a neighbor queue */
struct tsch_packet *
tsch_queue_remove_packet_from_queue(struct tsch_neighbor *n)
{
  if(!tsch_is_locked()) {
    if(n != NULL) {
      /* Get and remove packet from ringbuf (remove committed through an atomic operation */
      int16_t get_index = ringbufindex_get(&n->tx_ringbuf);
      if(get_index != -1) {
        return n->tx_array[get_index];
      } else {
        return NULL;
      }
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Free a packet */
void
tsch_queue_free_packet(struct tsch_packet *p)
{
  if(p != NULL) {
    queuebuf_free(p->qb);
    memb_free(&packet_memb, p);
  }
}
/*---------------------------------------------------------------------------*/
/* Updates neighbor queue state after a transmission */
int
tsch_queue_packet_sent(struct tsch_neighbor *n, struct tsch_packet *p,
                      struct tsch_link *link, uint8_t mac_tx_status)
{
  int in_queue = 1;
  int is_shared_link = link->link_options & LINK_OPTION_SHARED;
  int is_unicast = !n->is_broadcast;

  if(mac_tx_status == MAC_TX_OK) {
    /* Successful transmission */
    tsch_queue_remove_packet_from_queue(n);
    in_queue = 0;

    /* Update CSMA state in the unicast case */
    if(is_unicast) {
      if(is_shared_link || tsch_queue_is_empty(n)) {
        /* If this is a shared link, reset backoff on success.
         * Otherwise, do so only is the queue is empty */
        tsch_queue_backoff_reset(n);
      }
    }
  } else {
    /* Failed transmission */
    if(p->transmissions >= p->max_transmissions) {
      /* Drop packet */
      tsch_queue_remove_packet_from_queue(n);
      in_queue = 0;
    }
    /* Update CSMA state in the unicast case */
    if(is_unicast) {
      /* Failures on dedicated (== non-shared) leave the backoff
       * window nor exponent unchanged */
      if(is_shared_link) {
        /* Shared link: increment backoff exponent, pick a new window */
        tsch_queue_backoff_inc(n);
      }
    }
  }

  return in_queue;
}
/*---------------------------------------------------------------------------*/
/* Flush all neighbor queues */
void
tsch_queue_reset(void)
{
  /* Deallocate unneeded neighbors */
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      struct tsch_neighbor *next_n = list_item_next(n);
      /* Flush queue */
      tsch_queue_flush_nbr_queue(n);
      /* Reset backoff exponent */
      tsch_queue_backoff_reset(n);
      n = next_n;
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Deallocate neighbors with empty queue */
void
tsch_queue_free_unused_neighbors(void)
{
  /* Deallocate unneeded neighbors */
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      struct tsch_neighbor *next_n = list_item_next(n);
      /* Queue is empty, no tx link to this neighbor: deallocate.
       * Always keep time source and virtual broadcast neighbors. */
      if(!n->is_broadcast && !n->is_time_source && !n->tx_links_count
         && tsch_queue_is_empty(n)) {
        tsch_queue_remove_nbr(n);
      }
      n = next_n;
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Is the neighbor queue empty? */
int
tsch_queue_is_empty(const struct tsch_neighbor *n)
{
  return !tsch_is_locked() && n != NULL && ringbufindex_empty(&n->tx_ringbuf);
}
/*---------------------------------------------------------------------------*/
/* Returns the first packet from a neighbor queue */
struct tsch_packet *
tsch_queue_get_packet_for_nbr(const struct tsch_neighbor *n, struct tsch_link *link)
{
  if(!tsch_is_locked()) {
    int is_shared_link = link != NULL && link->link_options & LINK_OPTION_SHARED;
    if(n != NULL) {
      int16_t get_index = ringbufindex_peek_get(&n->tx_ringbuf);
      if(get_index != -1 &&
          !(is_shared_link && !tsch_queue_backoff_expired(n))) {    /* If this is a shared link,
                                                                    make sure the backoff has expired */
#if TSCH_WITH_LINK_SELECTOR
        int packet_attr_type = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_FRAME_TYPE);
        int packet_attr_metadata = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_MAC_METADATA);
        int packet_attr_slotframe = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_TSCH_SLOTFRAME);
        int packet_attr_timeslot = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_TSCH_TIMESLOT);
        
        // printf("link->slotframe_handle=%d\n", link->slotframe_handle);
        if(packet_attr_type == FRAME802154_DATAFRAME && packet_attr_metadata != 1) {
          if(n->is_time_source == 0 && packet_attr_slotframe == 2) {
            // printf("data packet not time source! ");
            // printf("packet_attr_slotframe=%d\n", packet_attr_slotframe);
            // packet_attr_slotframe = 3;
            // packet_attr_timeslot = 0xffff;
          }
        }
        if(packet_attr_type == FRAME802154_DATAFRAME && packet_attr_metadata == 1) {
          // printf("packet_attr_type=DATA & metadata\n");
        }         

        if(packet_attr_slotframe != 0xffff && packet_attr_slotframe != link->slotframe_handle) {
          // printf("packet_attr_slotframe NULL ");
          // printf("packet_attr_slotframe=%d link->slotframe_handle=%d\n", packet_attr_slotframe, link->slotframe_handle);
          return NULL;
        }
        if(packet_attr_timeslot != 0xffff && packet_attr_timeslot != link->timeslot) {
          // printf("packet_attr_timeslot NULL ");
          // printf("packet_attr_timeslot=%d link->timeslot=%d\n", packet_attr_timeslot, link->timeslot);
          return NULL;
        }
#endif
        return n->tx_array[get_index];
      }
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Returns the head packet from a neighbor queue (from neighbor address) */
struct tsch_packet *
tsch_queue_get_packet_for_dest_addr(const linkaddr_t *addr, struct tsch_link *link)
{
  if(!tsch_is_locked()) {
    return tsch_queue_get_packet_for_nbr(tsch_queue_get_nbr(addr), link);
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Returns the head packet of any neighbor queue with zero backoff counter.
 * Writes pointer to the neighbor in *n */
struct tsch_packet *
tsch_queue_get_unicast_packet_for_any(struct tsch_neighbor **n, struct tsch_link *link)
{
  if(!tsch_is_locked()) {
    struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
    struct tsch_packet *p = NULL;
    while(curr_nbr != NULL) {
      if(!curr_nbr->is_broadcast && curr_nbr->tx_links_count == 0) {
        /* Only look up for non-broadcast neighbors we do not have a tx link to */
        p = tsch_queue_get_packet_for_nbr(curr_nbr, link);
        if(p != NULL) {
          if(n != NULL) {
            *n = curr_nbr;
          }
          return p;
        }
      }
      curr_nbr = list_item_next(curr_nbr);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* May the neighbor transmit over a shared link? */
int
tsch_queue_backoff_expired(const struct tsch_neighbor *n)
{
  return n->backoff_window == 0;
}
/*---------------------------------------------------------------------------*/
/* Reset neighbor backoff */
void
tsch_queue_backoff_reset(struct tsch_neighbor *n)
{
  n->backoff_window = 0;
  n->backoff_exponent = TSCH_MAC_MIN_BE;
}
/*---------------------------------------------------------------------------*/
/* Increment backoff exponent, pick a new window */
void
tsch_queue_backoff_inc(struct tsch_neighbor *n)
{
  /* Increment exponent */
  n->backoff_exponent = MIN(n->backoff_exponent + 1, TSCH_MAC_MAX_BE);
  /* Pick a window (number of shared slots to skip). Ignore least significant
   * few bits, which, on some embedded implementations of rand (e.g. msp430-libc),
   * are known to have poor pseudo-random properties. */
  n->backoff_window = (random_rand() >> 6) % (1 << n->backoff_exponent);
  /* Add one to the window as we will decrement it at the end of the current slot
   * through tsch_queue_update_all_backoff_windows */
  n->backoff_window++;
}
/*---------------------------------------------------------------------------*/
/* Decrement backoff window for all queues directed at dest_addr */
void
tsch_queue_update_all_backoff_windows(const linkaddr_t *dest_addr)
{
  if(!tsch_is_locked()) {
    int is_broadcast = linkaddr_cmp(dest_addr, &tsch_broadcast_address);
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      if(n->backoff_window != 0 /* Is the queue in backoff state? */
         && ((n->tx_links_count == 0 && is_broadcast)
             || (n->tx_links_count > 0 && linkaddr_cmp(dest_addr, &n->addr)))) {
        n->backoff_window--;
      }
      n = list_item_next(n);
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Initialize TSCH queue module */
void
tsch_queue_init(void)
{
  list_init(neighbor_list);
  memb_init(&neighbor_memb);
  memb_init(&packet_memb);
  /* Add virtual EB and the broadcast neighbors */
  n_eb = tsch_queue_add_nbr(&tsch_eb_address);
  n_broadcast = tsch_queue_add_nbr(&tsch_broadcast_address);
}
/*---------------------------------------------------------------------------*/
/** @} */
