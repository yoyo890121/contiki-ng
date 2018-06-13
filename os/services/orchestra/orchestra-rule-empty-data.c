/*
 * Copyright (c) 2015, Swedish Institute of Computer Science.
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
 */
/**
 * \file
 *         Orchestra: a slotframe dedicated to unicast data transmission. Designed for
 *         RPL storing mode only, as this is based on the knowledge of the children (and parent).
 *         If receiver-based:
 *           Nodes listen at a timeslot defined as hash(MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *           Nodes transmit at: for each nbr in RPL children and RPL preferred parent,
 *                                             hash(nbr.MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *         If sender-based: the opposite
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "orchestra.h"
#include "net/packetbuf.h"
#include "tsch.h"
#include "sf-conf.h"

#include "sys/log.h"
#define LOG_MODULE "Orchestra"
#define LOG_LEVEL 4

static uint16_t slotframe_handle = 0;
static uint16_t channel_offset = 0;
static struct tsch_slotframe *sf_data;

/*---------------------------------------------------------------------------*/
static int
neighbor_has_uc_link(const linkaddr_t *linkaddr)
{
  // struct tsch_neighbor *n = tsch_queue_get_time_source();
  // if(n != NULL) {
     if(linkaddr != NULL && !linkaddr_cmp(linkaddr, &linkaddr_null)) {
      if(linkaddr_cmp(&orchestra_parent_linkaddr, linkaddr)) {
        // printf("data neighbor_has_uc_link 1.1\n");
        return 1;
      }  
    }  
  // }
  
  // printf("data neighbor_has_uc_link 0\n");
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
select_packet(uint16_t *slotframe, uint16_t *timeslot)
{
  /* Select data packets we have a unicast link to */
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == FRAME802154_DATAFRAME
  && packetbuf_attr(PACKETBUF_ATTR_MAC_METADATA) != 1 
  && neighbor_has_uc_link(dest)
  ) {
    if(slotframe != NULL) {
      *slotframe = slotframe_handle;
    }
    // printf("data select_packet 1, %d\n", *slotframe);
    return 1;
  }
  // printf("data select_packet 0\n");
  return 0;
}
/*---------------------------------------------------------------------------*/
static void
init(uint16_t sf_handle)
{
  slotframe_handle = sf_handle;
  channel_offset = sf_handle;
  /* Slotframe for unicast transmissions */
  sf_data = tsch_schedule_add_slotframe(slotframe_handle, SF_SLOTFRAME_LENGTH);
}
/*---------------------------------------------------------------------------*/
struct orchestra_rule empty_data = {
  init,
  NULL,
  select_packet,
  NULL,
  NULL,
};

