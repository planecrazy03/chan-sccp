/*!
 * \file 	sccp_event.c
 * \brief 	SCCP Event Class
 * \author 	Marcello Ceschia <marcello [at] ceschia.de>
 * \note 	This program is free software and may be modified and distributed under the terms of the GNU Public License.
 * \since	2009-09-02
 */

#ifndef SCCP_EVENT_H_
#define SCCP_EVENT_H_

#include "config.h"
#include "chan_sccp.h"

/* structures */

/* event types to notify modular systems */
/*!
 * \brief SCCP Event Type ENUM
 */
typedef enum {
	SCCP_EVENT_LINECREATED = 1,
	SCCP_EVENT_LINECHANGED = 1 << 1,
	SCCP_EVENT_LINEDELETED = 1 << 2,
	SCCP_EVENT_DEVICEATTACHED = 1 << 3,
	SCCP_EVENT_DEVICEDETACHED = 1 << 4,
	SCCP_EVENT_DEVICEREGISTERED = 1 << 5,
	SCCP_EVENT_DEVICEUNREGISTERED  = 1 << 6
} sccp_event_type_t;


typedef struct sccp_event sccp_event_t;
typedef void (*sccp_event_callback_t)(const sccp_event_t **event);

/*!
 * \brief SCCP Event Structure
 */
struct sccp_event {
	sccp_event_type_t	type;
	union sccp_event_data{
		struct{
			sccp_line_t 	*line;
		} lineCreated;
		struct{
			sccp_device_t 	*device;
		} deviceRegistered;
		struct{
			sccp_line_t 	*line;
			sccp_device_t	*device;
		} deviceAttached;
	} event;
};

typedef struct sccp_event_subscriber sccp_event_subscriber_t;
struct sccp_event_subscriber{
	sccp_event_type_t eventType;
	sccp_event_callback_t callback_function;

	SCCP_LIST_ENTRY(sccp_event_subscriber_t) list;
};

struct sccp_event_subscriptions{
	SCCP_LIST_HEAD(,sccp_event_subscriber_t)  subscriber;
};


struct sccp_event_subscriptions *sccp_event_listeners;


/* functions */

#define 	sccp_subscribe_event(event, callback)		\
			sccp_event_subscribe(event, callback,	__FILE__, __FUNCTION__, __LINE__);

void sccp_event_subscribe(sccp_event_type_t eventType, sccp_event_callback_t cb,
			     const char *file, const char *caller, int line);

void sccp_event_fire(const sccp_event_t **event);

#endif /* SCCP_EVENT_H_ */
