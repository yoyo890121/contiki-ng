/**
 * \file
 *      Bcollect resource
 * \author
 *      Green
 */

#include <string.h>
#include "coap-engine.h"
#include "coap.h"

#include "rpl.h"
#include "link-stats.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

static void res_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_periodic_handler(void);

PERIODIC_RESOURCE(res_bcollect,
                  "title=\"Binary collect\";obs",
                  res_get_handler,
                  res_post_handler,
                  NULL,
                  NULL,
                  1 * CLOCK_SECOND,
                  res_periodic_handler);

/*
 * Use local resource state that is accessed by res_get_handler() and altered by res_periodic_handler() or PUT or POST.
 */
static uint32_t event_counter = 0;

/* inter-packet time we generate a packet to send to observer */
uint8_t event_threshold = 20;

/* record last change event threshold's event_counter */
static uint32_t event_threshold_last_change = 0;

/* Record the packet have been generated. (Server perspective) */
static uint32_t packet_counter = 0;

static uint8_t packet_priority = 0;

#include "tsch.h"
extern struct tsch_asn_t tsch_current_asn;

static void
res_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  /*
   * For minimal complexity, request query and options should be ignored for GET on observable resources.
   * Otherwise the requests must be stored with the observer list and passed by REST.notify_subscribers().
   * This would be a TODO in the corresponding files in contiki/apps/erbium/!
   */

  struct 
  {
    // 32bits to 1 block
    uint8_t flag[2];  // 0 1
    uint8_t priority;
    // padding int8_t
    uint32_t start_asn; // 4 5 6 7
    uint32_t end_asn; // 8 9 10 11
    uint32_t event_counter; // 12 13 14 15
    uint8_t event_threshold; // 16
    // padding 3 int8_t and int16_t
    uint32_t event_threshold_last_change; 
    uint32_t packet_counter;
    unsigned char parent_address[2]; // uint8[0] , uint8[1]
    uint16_t rank;
    uint16_t parnet_link_etx;
    int16_t parent_link_rssi;
    uint8_t end_flag[2];
    // padding int16_t
  } message;

  memset(&message, 0, sizeof(message));

  message.flag[0] = 0x54;
  message.flag[1] = 0x66;
  message.end_flag[0] = 0xf0;
  message.end_flag[1] = 0xff;

  message.event_counter = event_counter;
  message.event_threshold = event_threshold;
  message.event_threshold_last_change = event_threshold_last_change;
  message.packet_counter = packet_counter;

  message.start_asn = tsch_current_asn.ls4b;

  message.priority = packet_priority;

  // uint8_t packet_length = 0;
  rpl_dag_t *dag;
  rpl_parent_t *preferred_parent;
  linkaddr_t parent;
  linkaddr_copy(&parent, &linkaddr_null);
  const struct link_stats *parent_link_stats;

  PRINTF("I am B_collect res_get hanlder!\n");
  coap_set_header_content_format(response, APPLICATION_OCTET_STREAM);
  coap_set_header_max_age(response, res_bcollect.periodic->period / CLOCK_SECOND);
  
  // packet_counter
  // memcpy(buffer,&packet_counter, sizeof(packet_counter));
  // packet_counter += sizeof(packet_counter);
  
  dag = rpl_get_any_dag();
  if(dag != NULL) {
    preferred_parent = dag->preferred_parent;
    if(preferred_parent != NULL) {
      uip_ds6_nbr_t *nbr;
      nbr = uip_ds6_nbr_lookup(rpl_neighbor_get_ipaddr(preferred_parent));
      if(nbr != NULL) {
        /* Use parts of the IPv6 address as the parent address, in reversed byte order. */
        parent.u8[LINKADDR_SIZE - 1] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 2];
        parent.u8[LINKADDR_SIZE - 2] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 1];
        parent_link_stats = rpl_neighbor_get_link_stats(preferred_parent);
        message.parnet_link_etx = parent_link_stats->etx;
        message.parent_link_rssi = parent_link_stats->rssi;
      }
    }
    message.rank = dag->rank;
  } else {
  }

  message.parent_address[0] = parent.u8[LINKADDR_SIZE - 1];
  message.parent_address[1] = parent.u8[LINKADDR_SIZE - 2];

  memcpy(buffer, &message, sizeof(message));

  // rpl things
  // rpl_dag
  // rpl_instance_t* rpl_now = rpl_get_instance();
  // rpl_parent_t* rpl_now_parent = rpl_now->current_dag->preferred_parent;
  // uip_ipaddr_t* rpl_parent_address = rpl_get_parent_ipaddr(rpl_now_parent);
  // memcpy(buffer, rpl_parent_address, sizeof(rpl_parent_address));
  // packet_counter += sizeof(rpl_parent_address);

  coap_set_uip_traffic_class(packet_priority);
  coap_set_payload(response, buffer, sizeof(message));

  // coap_set_payload(response, buffer, snprintf((char *)buffer, preferred_size, "[Collect] ec: %lu, et: %lu, lc, %lu, pc: %lu", event_counter, event_threshold, event_threshold_last_change,packet_counter));

  /* The REST.subscription_handler() will be called for observable resources by the REST framework. */
}


/* Used for update the threshold */
static void
res_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *threshold_c = NULL;
  const char *priority_c = NULL;
  int threshold = -1;
  int priority = -1;

  if(coap_get_query_variable(request, "thd", &threshold_c)) {
    threshold = (uint8_t)atoi(threshold_c);
  }
  if(coap_get_query_variable(request, "pp", &priority_c)) {
    priority = (uint8_t)atoi(priority_c);
  }

  if(threshold < 1 && (priority < 0 || priority > 2)) {
    /* Threashold is too small ignore it! */
    coap_set_status_code(response, BAD_REQUEST_4_00);
  } else {
    if(threshold >= 1) {
      /* Update to new threshold */
      event_threshold = threshold;
      event_threshold_last_change = event_counter;
    }
    if(priority >= 0 && priority <= 2) {
      packet_priority = priority;
    }
  }
}

/*
 * Additionally, a handler function named [resource name]_handler must be implemented for each PERIODIC_RESOURCE.
 * It will be called by the REST manager process with the defined period.
 */
static void
res_periodic_handler()
{
  /* This periodic handler will be called every second */
  ++event_counter;

  /* Will notify subscribers when inter-packet time is match */
  if(event_counter % event_threshold == 0) {
    ++packet_counter;
    PRINTF("Generate a new packet! , %08x. \n", tsch_current_asn.ls4b);
        
    /* Notify the registered observers which will trigger the res_get_handler to create the response. */
    coap_notify_observers(&res_bcollect);
  }
}
