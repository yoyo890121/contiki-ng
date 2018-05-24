/*
 * Copyright (c) 2016, Yasuyuki Tanaka
 * Copyright (c) 2016, Centre for Development of Advanced Computing (C-DAC).
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
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 *         A 6P Simple Schedule Function
 * \author
 *         Shalu R <shalur@cdac.in>
 *         Lijo Thomas <lijo@cdac.in>
 *         Yasuyuki Tanaka <yasuyuki.tanaka@inf.ethz.ch>
 */

#include "contiki-lib.h"

#include "lib/assert.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/mac/tsch/sixtop/sixp.h"
#include "net/mac/tsch/sixtop/sixp-nbr.h"
#include "net/mac/tsch/sixtop/sixp-pkt.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"

#include "sf-simple.h"
#include "sf-conf.h"

#define DEBUG DEBUG_PRINT
#include "net/net-debug.h"

PROCESS_NAME(sf_wait_trans_done_process);
process_event_t sf_trans_done;
PROCESS(sf_wait_for_retry_process, "Wait for retry process");

typedef struct {
  uint16_t timeslot_offset;
  uint16_t channel_offset;
} sf_simple_cell_t;

static const uint16_t slotframe_handle = SF_SLOTFRAME_HANDLE;
static uint8_t res_storage[4 + SF_SIMPLE_MAX_LINKS * 4];
static uint8_t req_storage[4 + SF_SIMPLE_MAX_LINKS * 4];

static void read_cell(const uint8_t *buf, sf_simple_cell_t *cell);
static void print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len);
static void add_links_to_schedule(const linkaddr_t *peer_addr,
                                  uint8_t link_option,
                                  const uint8_t *cell_list,
                                  uint16_t cell_list_len);
static void remove_links_to_schedule(const uint8_t *cell_list,
                                     uint16_t cell_list_len);
static void add_response_sent_callback(void *arg, uint16_t arg_len,
                                       const linkaddr_t *dest_addr,
                                       sixp_output_status_t status);
static void delete_response_sent_callback(void *arg, uint16_t arg_len,
                                          const linkaddr_t *dest_addr,
                                          sixp_output_status_t status);
static void add_req_input(const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void delete_req_input(const uint8_t *body, uint16_t body_len,
                             const linkaddr_t *peer_addr);
static void input(sixp_pkt_type_t type, sixp_pkt_code_t code,
                  const uint8_t *body, uint16_t body_len,
                  const linkaddr_t *src_addr);
static void request_input(sixp_pkt_cmd_t cmd,
                          const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void response_input(sixp_pkt_rc_t rc,
                           const uint8_t *body, uint16_t body_len,
                           const linkaddr_t *peer_addr);

/*
 * scheduling policy:
 * add: if and only if all the requested cells are available, accept the request
 * delete: if and only if all the requested cells are in use, accept the request
 */

static void
read_cell(const uint8_t *buf, sf_simple_cell_t *cell)
{
  cell->timeslot_offset = buf[0] + (buf[1] << 8);
  cell->channel_offset = buf[2] + (buf[3] << 8);
}

static void
print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len)
{
  uint16_t i;
  sf_simple_cell_t cell;

  for(i = 0; i < (cell_list_len / sizeof(cell)); i++) {
    read_cell(&cell_list[i], &cell);
    PRINTF("%u ", cell.timeslot_offset);
  }
}

static void
add_links_to_schedule(const linkaddr_t *peer_addr, uint8_t link_option,
                      const uint8_t *cell_list, uint16_t cell_list_len)
{
  PRINTF("in add_links_to_schedule\n");
  /* add only the first valid cell */

  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  struct tsch_link *link;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    PRINTF("slotframe == NULL\n");
    return;
  }

  for(i = 0; i < (cell_list_len / sizeof(cell)); i++) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    PRINTF("sf-simple: Schedule link %d as %s with node %u\n",
           cell.timeslot_offset,
           link_option == LINK_OPTION_RX ? "RX" : "TX",
           peer_addr->u8[7]);
    link = tsch_schedule_add_link(slotframe,
                           link_option, LINK_TYPE_NORMAL, &tsch_broadcast_address,
                           cell.timeslot_offset, cell.channel_offset);
    link->real_addr = *peer_addr;
    break;
  }
  process_post(&sf_wait_trans_done_process, sf_trans_done, NULL);
}

static void
remove_links_to_schedule(const uint8_t *cell_list, uint16_t cell_list_len)
{
  PRINTF("in remove_links_to_schedule\n");
  /* remove all the cells */

  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    PRINTF("slotframe == NULL\n");
    return;
  }

  for(i = 0; i < (cell_list_len / sizeof(cell)); i++) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    tsch_schedule_remove_link_by_timeslot(slotframe,
                                          cell.timeslot_offset);
  }
}

static void
add_response_sent_callback(void *arg, uint16_t arg_len,
                           const linkaddr_t *dest_addr,
                           sixp_output_status_t status)
{
  PRINTF("in add_response_sent_callback\n");
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    add_links_to_schedule(dest_addr, LINK_OPTION_RX,
                          cell_list, cell_list_len);
  }
}

static void
delete_response_sent_callback(void *arg, uint16_t arg_len,
                              const linkaddr_t *dest_addr,
                              sixp_output_status_t status)
{
  PRINTF("in delete_response_sent_callback\n");
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    remove_links_to_schedule(cell_list, cell_list_len);
  }
}

static void
add_req_input(const uint8_t *body, uint16_t body_len, const linkaddr_t *peer_addr)
{
  PRINTF("in add_req_input\n");
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int feasible_link;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    PRINTF("sf-simple: Parse error on add request\n");
    return;
  }

  PRINTF("sf-simple: Received a 6P Add Request for %d links from node %d with LinkList : ",
         num_cells, peer_addr->u8[7]);
  print_cell_list(cell_list, cell_list_len);
  PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    PRINTF("slotframe == NULL\n");
    return;
  }

  if(num_cells > 0 && cell_list_len > 0) {
    memset(res_storage, 0, sizeof(res_storage));
    res_len = 0;

    /* checking availability for requested slots */
    for(i = 0, feasible_link = 0;
        i < cell_list_len && feasible_link < num_cells;
        i += sizeof(cell)) {
      read_cell(&cell_list[i], &cell);
      if(tsch_schedule_get_link_by_timeslot(slotframe,
                                            cell.timeslot_offset) == NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               feasible_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
        feasible_link++;
      }
    }

    if(feasible_link == num_cells) {
      /* Links are feasible. Create Link Response packet */
      PRINTF("sf-simple: Send a 6P Response to node %d\n", peer_addr->u8[7]);
      sixp_output(SIXP_PKT_TYPE_RESPONSE,
                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                  SF_SIMPLE_SFID,
                  res_storage, res_len, peer_addr,
                  add_response_sent_callback, res_storage, res_len);
    }
  }
}

static void
delete_req_input(const uint8_t *body, uint16_t body_len,
                 const linkaddr_t *peer_addr)
{
  PRINTF("in delete_req_input\n");
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;
  int removed_link;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    PRINTF("sf-simple: Parse error on delete request\n");
    return;
  }

  PRINTF("sf-simple: Received a 6P Delete Request for %d links from node %d with LinkList : ",
         num_cells, peer_addr->u8[7]);
  print_cell_list(cell_list, cell_list_len);
  PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    PRINTF("slotframe == NULL\n");
    return;
  }

  memset(res_storage, 0, sizeof(res_storage));
  res_len = 0;

  if(num_cells > 0 && cell_list_len > 0) {
    /* ensure before delete */
    for(i = 0, removed_link = 0; i < (cell_list_len / sizeof(cell)); i++) {
      read_cell(&cell_list[i], &cell);
      if(tsch_schedule_get_link_by_timeslot(slotframe,
                                            cell.timeslot_offset) != NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               removed_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
      }
    }
  }

  /* Links are feasible. Create Link Response packet */
  PRINTF("sf-simple: Send a 6P Response to node %d\n", peer_addr->u8[7]);
  sixp_output(SIXP_PKT_TYPE_RESPONSE,
              (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
              SF_SIMPLE_SFID,
              res_storage, res_len, peer_addr,
              delete_response_sent_callback, res_storage, res_len);
}

static void
input(sixp_pkt_type_t type, sixp_pkt_code_t code,
      const uint8_t *body, uint16_t body_len, const linkaddr_t *src_addr)
{
  assert(body != NULL && body != NULL);
  switch(type) {
    case SIXP_PKT_TYPE_REQUEST:
      request_input(code.cmd, body, body_len, src_addr);
      break;
    case SIXP_PKT_TYPE_RESPONSE:
      response_input(code.cmd, body, body_len, src_addr);
      break;
    default:
      /* unsupported */
      break;
  }
}

static void
request_input(sixp_pkt_cmd_t cmd,
              const uint8_t *body, uint16_t body_len,
              const linkaddr_t *peer_addr)
{
  PRINTF("in request_input\n");
  assert(body != NULL && peer_addr != NULL);

  switch(cmd) {
    case SIXP_PKT_CMD_ADD:
      add_req_input(body, body_len, peer_addr);
      break;
    case SIXP_PKT_CMD_DELETE:
      delete_req_input(body, body_len, peer_addr);
      break;
    default:
      /* unsupported request */
      break;
  }
}
static void
response_input(sixp_pkt_rc_t rc,
               const uint8_t *body, uint16_t body_len,
               const linkaddr_t *peer_addr)
{
  PRINTF("in response_input\n");
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;
  sixp_trans_t *trans;

  assert(body != NULL && peer_addr != NULL);

  if((nbr = sixp_nbr_find(peer_addr)) == NULL ||
     (trans = sixp_trans_find(peer_addr)) == NULL) {
       PRINTF("in NULL\n");
    return;
  }

  if(rc == SIXP_PKT_RC_SUCCESS) {
    switch(sixp_trans_get_cmd(trans)) {
      case SIXP_PKT_CMD_ADD:
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          PRINTF("sf-simple: Parse error on add response\n");
          return;
        }
        PRINTF("sf-simple: Received a 6P Add Response with LinkList : ");
        print_cell_list(cell_list, cell_list_len);
        PRINTF("\n");
        add_links_to_schedule(peer_addr, LINK_OPTION_TX,
                              cell_list, cell_list_len);
        break;
      case SIXP_PKT_CMD_DELETE:
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          PRINTF("sf-simple: Parse error on add response\n");
          return;
        }
        PRINTF("sf-simple: Received a 6P Delete Response with LinkList : ");
        print_cell_list(cell_list, cell_list_len);
        PRINTF("\n");
        remove_links_to_schedule(cell_list, cell_list_len);
        break;
      case SIXP_PKT_CMD_COUNT:
      case SIXP_PKT_CMD_LIST:
      case SIXP_PKT_CMD_CLEAR:
      default:
        PRINTF("sf-simple: unsupported response\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link addition
 */
int 
sf_simple_add_links(linkaddr_t *peer_addr, uint8_t num_links)
{
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  uint8_t req_len;
  sf_simple_cell_t cell_list[SF_SIMPLE_MAX_LINKS];

  /* Flag to prevent repeated slots */
  uint8_t slot_check = 1;
  uint16_t random_slot = 0;
  uint16_t random_channel = 0;

  assert(peer_addr != NULL && sf != NULL);

  do {
    /* Randomly select a slot offset within SF_SLOTFRAME_LENGTH */
    random_slot = ((random_rand() & 0xFF)) % SF_SLOTFRAME_LENGTH;
    random_channel = ((random_rand() & 0xFF)) % SF_CHANNEL_NUM;

    if(tsch_schedule_get_link_by_timeslot(sf, random_slot) == NULL) {

      /* To prevent repeated slots */
      for(i = 0; i < index; i++) {
        if(cell_list[i].timeslot_offset != random_slot) {
          /* Random selection resulted in a free slot */
          if(i == index - 1) { /* Checked till last index of link list */
            slot_check = 1;
            break;
          }
        } else {
          /* Slot already present in CandidateLinkList */
          slot_check++;
          break;
        }
      }

      /* Random selection resulted in a free slot, add it to linklist */
      if(slot_check == 1) {
        cell_list[index].timeslot_offset = random_slot;
        cell_list[index].channel_offset = slotframe_handle+random_channel;

        index++;
        slot_check++;
      } else if(slot_check > SF_SLOTFRAME_LENGTH) {
        PRINTF("sf-simple:! Number of trials for free slot exceeded...\n");
        return -1;
        break; /* exit while loop */
      }
    }
  } while(index < SF_SIMPLE_MAX_LINKS);

  /* Create a Sixtop Add Request. Return 0 if Success */
  if(index == 0 ) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_cell_options(SIXP_PKT_TYPE_REQUEST,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                               SIXP_PKT_CELL_OPTION_TX,
                               req_storage,
                               sizeof(req_storage)) != 0 ||
     sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            num_links,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            (const uint8_t *)cell_list,
                            index * sizeof(sf_simple_cell_t), 0,
                            req_storage, sizeof(req_storage)) != 0) {
    PRINTF("sf-simple: Build error on add request\n");
    return -1;
  }

  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + index * sizeof(sf_simple_cell_t);
  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  PRINTF("sf-simple: Send a 6P Add Request for %d links to node %d with LinkList : ",
         num_links, peer_addr->u8[7]);
  print_cell_list((const uint8_t *)cell_list, index * sizeof(sf_simple_cell_t));
  PRINTF("\n");

  return 0;
}
/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link deletion
 */
int
sf_simple_remove_links(linkaddr_t *peer_addr)
{
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;

  uint16_t req_len;
  sf_simple_cell_t cell;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < SF_SLOTFRAME_LENGTH; i++) {
    l = tsch_schedule_get_link_by_timeslot(sf, i);

    if(l) {
      /* Non-zero value indicates a scheduled link */
      if(((linkaddr_cmp(&l->addr, peer_addr)) || (linkaddr_cmp(&l->real_addr, peer_addr))) && (l->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        index++;
        break;   /* delete atmost one */
      }
    }
  }

  if(index == 0) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            1,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            (const uint8_t *)&cell, sizeof(cell),
                            0,
                            req_storage, sizeof(req_storage)) != 0) {
    PRINTF("sf-simple: Build error on add request\n");
    return -1;
  }
  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + sizeof(sf_simple_cell_t);

  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  PRINTF("sf-simple: Send a 6P Delete Request for %d links to node %d with LinkList : ",
         1, peer_addr->u8[7]);
  print_cell_list((const uint8_t *)&cell, sizeof(cell));
  PRINTF("\n");

  return 0;
}

typedef struct {
  sixp_pkt_cmd_t cmd;
  const linkaddr_t *peer_addr;
} process_data;

static void
timeout(sixp_pkt_cmd_t cmd, const linkaddr_t *peer_addr)
{
  PRINTF("transaction timeout\n");
  process_data data_to_process = {cmd, peer_addr};
  process_start(&sf_wait_for_retry_process, &data_to_process);
}

#define TIMEOUT_RANDOM 3

PROCESS_THREAD(sf_wait_for_retry_process, ev, data)
{
  static struct etimer et;
  static sixp_pkt_cmd_t cmd;
  static const linkaddr_t *peer_addr_c;
  static linkaddr_t peer_addr; //const problem
  uint8_t random_time = (((random_rand() & 0xFF)) % TIMEOUT_RANDOM)+1;

  PROCESS_BEGIN();
  
  etimer_set(&et, CLOCK_SECOND*random_time);
  cmd = ((process_data *)data)->cmd;
  peer_addr_c = ((process_data *)data)->peer_addr;
  peer_addr = *peer_addr_c;
  printf("in sf_wait_for_retry_process cmd=%d node=%d\n", cmd, peer_addr.u8[7]);  

  sixp_trans_t *trans = sixp_trans_find(&peer_addr);
  if(trans != NULL) {
    sixp_trans_state_t state;
    state = sixp_trans_get_state(trans);
    if (state == SIXP_TRANS_STATE_REQUEST_SENT || state == SIXP_TRANS_STATE_RESPONSE_SENT || state == SIXP_TRANS_STATE_CONFIRMATION_SENT) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
      PRINTF("do retry\n");
      switch (cmd) {
      case SIXP_PKT_CMD_ADD:
        sf_simple_add_links(&peer_addr, 1);
        break;
      case SIXP_PKT_CMD_DELETE:
        sf_simple_remove_links(&peer_addr);
        break;
      default:
        /* unsupported request */
        break;
      }
    }
  }
    
  etimer_reset(&et);  
  PROCESS_END();
}

static void
init(void)
{
  sf_trans_done = process_alloc_event();
}

const sixtop_sf_t sf_simple_driver = {
  SF_SIMPLE_SFID,
  CLOCK_SECOND*4,
  init,
  input,
  timeout
};
