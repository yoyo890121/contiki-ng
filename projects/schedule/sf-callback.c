#include "contiki-lib.h"
#include "lib/assert.h"
#include "tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/mac/tsch/sixtop/sixp-nbr.h"
#include "net/mac/tsch/sixtop/sixp-pkt.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"

#include "sf-callback.h"
#include "sf-simple.h"
#include "sf-conf.h"

#define DEBUG DEBUG_NONE
#include "net/net-debug.h"

extern process_event_t sf_trans_done;

PROCESS(sf_wait_trans_done_process, "sf wait trans done process");
PROCESS(sf_housekeeping_process, "sf housekeeping process");

typedef struct {
  uint16_t timeslot_offset;
  uint16_t channel_offset;
} sf_simple_cell_t;

// static uint8_t res_storage[4 + SF_SIMPLE_MAX_LINKS * 4];
static uint8_t req_storage[4 + SF_SIMPLE_MAX_LINKS * 4];


static void read_cell(const uint8_t *buf, sf_simple_cell_t *cell);
static void print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len);
int remove_all_links(linkaddr_t *peer_addr);
static void remove_all_unused_links();

void
sf_simple_switching_parent_callback(linkaddr_t *old_addr, linkaddr_t *new_addr)
{
  printf("in sf_simple_switching_parent_callback\n");
  struct tsch_neighbor *n = NULL;
  n = tsch_queue_get_nbr(&tsch_broadcast_address);
  uint8_t dedicated_links_num =  n->dedicated_tx_links_count-1; //-1 for EB slotframe Tx

  if(sf_simple_add_links(new_addr, dedicated_links_num) == 0){
    printf("Add to new parent success\n");
    process_start(&sf_wait_trans_done_process, old_addr);
    // TODO clear cmd old_addr;
  }

}

PROCESS_THREAD(sf_wait_trans_done_process, ev, data)
{
  static linkaddr_t *old_addr = NULL;
  static struct etimer et;

  PROCESS_BEGIN();
  etimer_set(&et, CLOCK_SECOND * 1);
  old_addr = data;
  PROCESS_WAIT_EVENT_UNTIL(ev == sf_trans_done);
  PROCESS_YIELD_UNTIL(etimer_expired(&et));
  printf("sf_trans_done\n");
  remove_all_links(old_addr);
  PROCESS_END();
}

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

int remove_all_links(linkaddr_t *peer_addr)
{
  printf("in remove_all_links\n");
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(SF_SLOTFRAME_HANDLE);
  struct tsch_link *link = NULL;

  uint16_t req_len;
  sf_simple_cell_t cell_list[SF_SIMPLE_MAX_LINKS];

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < SF_SLOTFRAME_LENGTH; i++) {
    link = tsch_schedule_get_link_by_timeslot(sf, i);

    if(link) {
      /* Non-zero value indicates a scheduled link */
      if(((linkaddr_cmp(&link->addr, peer_addr)) || (linkaddr_cmp(&link->real_addr, peer_addr))) && (link->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        // tsch_schedule_remove_link(sf, link);
        cell_list[index].timeslot_offset = i;
        cell_list[index].channel_offset = link->channel_offset;
        index++;
      }
    }
  }

  if(index == 0 ) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            index,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            (const uint8_t *)cell_list,
                            index * sizeof(sf_simple_cell_t),
                            0,
                            req_storage, sizeof(req_storage)) != 0) {
    PRINTF("sf-simple: Build error on delete all request\n");
    return -1;
  }

  req_len = 4 + index * sizeof(sf_simple_cell_t);
  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  PRINTF("sf-simple: Send a 6P Delete Request for %d links to node %d with LinkList :",
         index, peer_addr->u8[7]);
  print_cell_list((const uint8_t *)cell_list, index * sizeof(sf_simple_cell_t));
  PRINTF("\n");

  return 0;
}

PROCESS_THREAD(sf_housekeeping_process, ev, data)
{
  static struct etimer et;
  PROCESS_BEGIN();

  etimer_set(&et, CLOCK_SECOND * HOUSEKEEPING_PERIOD);
  while(1) {
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
    etimer_reset(&et);
    remove_all_unused_links();

    struct tsch_neighbor *parent = tsch_queue_get_time_source();
    struct tsch_slotframe *sf =
              tsch_schedule_get_slotframe_by_handle(SF_SLOTFRAME_HANDLE);
    struct tsch_link *l = NULL;
    uint8_t links_count = 0;
    if(parent != NULL) {
      for (uint8_t i = 0; i < SF_SLOTFRAME_LENGTH; i++) {
        l = tsch_schedule_get_link_by_timeslot(sf, i);
        if (l) {
          /* Non-zero value indicates a scheduled link */
          if (((linkaddr_cmp(&l->addr, &parent->addr)) || (linkaddr_cmp(&l->real_addr, &parent->addr))) && (l->link_options == LINK_OPTION_TX)) {
            links_count++;
          }
        }
      }
      printf("links_count=%d\n", links_count);
      if(links_count == 0) {
        sf_simple_add_links(&parent->addr, 1);
        printf("links_count == 0 add a link\n");
      }
    }
  }

  PROCESS_END();
}

static void
remove_all_unused_links()
{
  struct tsch_neighbor *parent = tsch_queue_get_time_source();
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(SF_SLOTFRAME_HANDLE);
  struct tsch_link *link = NULL;

  if(parent != NULL) {
    for(uint8_t i = 0; i < SF_SLOTFRAME_LENGTH; i++) {
      link = tsch_schedule_get_link_by_timeslot(sf, i);
  
      if(link) {
        /* Non-zero value indicates a scheduled link */
        if(!(linkaddr_cmp(&link->real_addr, &parent->addr)) && (link->link_options == LINK_OPTION_TX)) {
          tsch_schedule_remove_link(sf, link);
          printf("remove_not_parent_tx_link\n");
        }  
        rpl_nbr_t* neighbor = rpl_neighbor_get_from_lladdr((uip_lladdr_t*)(&link->real_addr));
        if(neighbor == NULL) {
          tsch_schedule_remove_link(sf, link);
          printf("remove_not_neighbor_link\n"); //seem does not work
        } else {
          if(!rpl_neighbor_is_reachable(neighbor)) {
            printf("remove_not_reachable_neighbor_link\n");
          }
        }
      }
    }
  }
}
