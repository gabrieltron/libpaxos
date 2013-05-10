#ifndef _LEARNER_H_
#define _LEARNER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "libpaxos.h"
#include "libpaxos_messages.h"

struct learner;

struct learner* learner_new(int instances);
void learner_receive_accept(struct learner* s, accept_ack* ack);
accept_ack* learner_deliver_next(struct learner* s);

#ifdef __cplusplus
}
#endif

#endif
