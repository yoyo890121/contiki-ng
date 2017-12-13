/**
 * \file
 *      Collect resource
 * \author
 *      Green
 */

#include <string.h>
#include <stdlib.h> 
#include "rest-engine.h"
#include "coap.h"
#include "os/sys/clock.h"

#define DEBUG 1
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


static void res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_periodic_handler(void);

PERIODIC_RESOURCE(res_collect,
                  "title=\"Periodic collect\";obs",
                  res_get_handler,
                  res_post_handler,
                  NULL,
                  NULL,
                  1 * CLOCK_SECOND,
                  res_periodic_handler);

/*
 * Use local resource state that is accessed by res_get_handler() and altered by res_periodic_handler() or PUT or POST.
 */
static int32_t event_counter = 0;

/* inter-packet time we generate a packet to send to observer */
static int8_t event_threshold = 20;

/* record last change event threshold's event_counter */
static int32_t event_threshold_last_change = 0;

/* Record the packet have been generated. (Server perspective) */
static int32_t packet_counter = 0;

static void
res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  /*
   * For minimal complexity, request query and options should be ignored for GET on observable resources.
   * Otherwise the requests must be stored with the observer list and passed by REST.notify_subscribers().
   * This would be a TODO in the corresponding files in contiki/apps/erbium/!
   */
  PRINTF("I am collect res_get hanlder!\n");
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  REST.set_header_max_age(response, res_collect.periodic->period / CLOCK_SECOND);

  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "[Collect] ec: %lu, et: %d, lc, %lu, pc: %lu", event_counter, event_threshold, event_threshold_last_change,packet_counter));

  /* The REST.subscription_handler() will be called for observable resources by the REST framework. */
}


/* Used for update the threshold */
static void
res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *threshold_c = NULL;
  int threshold = -1;
  if(REST.get_query_variable(request, "threshold", &threshold_c)) {
    threshold = (int8_t)atoi(threshold_c);
  }

  if(threshold < 1) {
    /* Threashold is too smaill ignore it! */
    REST.set_response_status(response, REST.status.BAD_REQUEST);
  } else {
    /* Update to new threshold */
    event_threshold = threshold;
    event_threshold_last_change = event_counter;
  }
}
#if CONTIKI_TARGET_CC2538DK
uint64_t get_clock_epoch(void);
uint64_t get_clock_update_counter(void);
uint64_t get_software_rtimer_ticks(void);

uint32_t get_high_phy_rtimer(void);
uint64_t get_high_sof_rtimer(void);
uint64_t get_last_update_done_rtimer(void);
uint64_t get_high_last_update_done_rtimer(void);

uint32_t get_strange_happen_counter(void);

#endif

/*
 * Additionally, a handler function named [resource name]_handler must be implemented for each PERIODIC_RESOURCE.
 * It will be called by the REST manager process with the defined period.
 */
static void
res_periodic_handler()
{
  /* This periodic handler will be called every second */
  ++event_counter;
  
#if CONTIKI_TARGET_CC2538DK
  static clock_time_t clock_g;
  rtimer_clock_t rtimer_now;
  clock_time_t timer_start = periodic_res_collect.periodic_timer.timer.start;

  clock_g = clock_time();
  rtimer_now = RTIMER_NOW();
  PRINTF("clock: %lu ticks\n", clock_g);
  PRINTF("NextT: %lu ticks\n", timer_start);

  uint64_t rt_clock_epoch;
  rt_clock_epoch = get_clock_epoch();
  uint64_t software_rtimer = get_software_rtimer_ticks();
  uint64_t software_rtimer_update_counter = get_clock_update_counter();

  uint32_t high_phy_rtimer = get_high_phy_rtimer();
  uint64_t high_sof_rtimer = get_high_sof_rtimer();
  uint64_t last_update_done_rtimer = get_last_update_done_rtimer();
  uint64_t high_last_update_done_rtimer = get_high_last_update_done_rtimer();
  uint32_t strange_happen_counter = get_strange_happen_counter();
  
  PRINTF("phy_rtime   : %lu \n", rtimer_now);
  PRINTF("sof_rtime   : %llu \n", software_rtimer);
  PRINTF("last_update : %llu \n", last_update_done_rtimer);
  PRINTF("epoch    : %llu \n", rt_clock_epoch);
  PRINTF("update count: %llu \n", software_rtimer_update_counter);


  PRINTF("high_phy_rtime: %lu \n", high_phy_rtimer);
  PRINTF("high_sof_rtime: %llu \n", high_sof_rtimer);
  PRINTF("strange_count : %lu \n", strange_happen_counter);
  PRINTF("high_last_upda: %llu \n", high_last_update_done_rtimer);

  PRINTF("================\n");
#endif

  /* Will notify subscribers when inter-packet time is match */
  if(event_counter % event_threshold == 0) {
    ++packet_counter;
    PRINTF("Generate a new packet!\n");
    /* Notify the registered observers which will trigger the res_get_handler to create the response. */
    REST.notify_subscribers(&res_collect);
  }
}
