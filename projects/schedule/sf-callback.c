
#include "lib/assert.h"
#include "tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "sf-callback.h"
#include "sf-simple.h"
#include "sf-conf.h"

#include <stdio.h>

void remove_links(linkaddr_t *peer_addr);

void
sf_simple_switching_parent_callback(linkaddr_t *old_addr, linkaddr_t *new_addr)
{
  printf("in sf_simple_switching_parent_callback\n");
  struct tsch_neighbor *n = NULL;
  n = tsch_queue_get_nbr(&tsch_broadcast_address);
  uint8_t dedicated_links_num =  n->dedicated_tx_links_count-1; //-1 for EB slotframe Tx
  if(sf_simple_add_links(new_addr, dedicated_links_num) == 0){
    printf("Add to new parent success\n");
    remove_links(old_addr); //temporary implement, only remove child link
    // TODO clear cmd old_addr;
  }

}

void remove_links(linkaddr_t *peer_addr)
{
  uint8_t i = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(SF_SLOTFRAME_HANDLE);
  struct tsch_link *link;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < SF_SLOTFRAME_LENGTH; i++) {
    link = tsch_schedule_get_link_by_timeslot(sf, i);

    if(link) {
      /* Non-zero value indicates a scheduled link */
      if(((linkaddr_cmp(&link->addr, peer_addr)) || (linkaddr_cmp(&link->real_addr, peer_addr))) && (link->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        tsch_schedule_remove_link(sf, link);
      }
    }
  }
}