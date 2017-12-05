
#ifndef ER_OBSERVE_CLIENT_H_
#define ER_OBSERVE_CLIENT_H_

#include "contiki.h"

void notification_callback(void);
void toggle_observation(void);

PROCESS_NAME(er_example_observe_client);

#endif /* ER_OBSERVE_CLIENT_H_ */
