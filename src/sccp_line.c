/*!
 * \file 	sccp_line.c
 * \brief 	SCCP Line
 * \author 	Sergio Chersovani <mlists [at] c-net.it>
 * \note	Reworked, but based on chan_sccp code.
 *        	The original chan_sccp driver that was made by Zozo which itself was derived from the chan_skinny driver.
 *        	Modified by Jan Czmok and Julien Goodwin
 * \note 	This program is free software and may be modified and distributed under the terms of the GNU Public License.
 * \version 	$LastChangedDate$
 */

#include "config.h"

#ifndef ASTERISK_CONF_1_2
#include <asterisk.h>
#endif
#include "chan_sccp.h"
#include "sccp_lock.h"
#include "sccp_line.h"
#include "sccp_utils.h"
#include "sccp_device.h"
#include "sccp_actions.h"
#include "sccp_channel.h"
#include "sccp_features.h"
#include "sccp_mwi.h"
#include <asterisk/utils.h>


/*!
 * \brief Build Default SCCP Line.
 *
 * Creates an SCCP Line with default/global values
 *
 * \return Default SCCP Line
 */
sccp_line_t * sccp_line_create(void) {
	sccp_line_t * l = ast_malloc(sizeof(sccp_line_t));
	if (!l) {
		sccp_log(0)(VERBOSE_PREFIX_3 "Unable to allocate memory for a line\n");
		return NULL;
	}
	memset(l, 0, sizeof(sccp_line_t));
	ast_mutex_init(&l->lock);
	SCCP_LIST_HEAD_INIT(&l->channels);
	SCCP_LIST_HEAD_INIT(&l->devices);
	SCCP_LIST_HEAD_INIT(&l->mailboxes);
	return sccp_line_applyDefaults(l);
}

/*!
 * \brief Apply Default Configuration to SCCP Line
 * \param l SCCP Line
 * \return SCCP line
 */
sccp_line_t *sccp_line_applyDefaults(sccp_line_t *l)
{
	if(!l)
		return NULL;

	//l->instance = -1;
	l->incominglimit = 3; /* default value */
	l->echocancel = GLOB(echocancel); /* default value */
	l->silencesuppression = GLOB(silencesuppression); /* default value */
	l->rtptos = GLOB(rtptos); /* default value */
	l->transfer = 1; /* default value. on if the device transfer is on*/
	l->secondary_dialtone_tone = SKINNY_TONE_OUTSIDEDIALTONE;
	l->dndmode = SCCP_DNDMODE_OFF;
	//l->mailbox[0] = '\0';

	sccp_copy_string(l->context, GLOB(context), sizeof(l->context));
	sccp_copy_string(l->language, GLOB(language), sizeof(l->language));
	sccp_copy_string(l->accountcode, GLOB(accountcode), sizeof(l->accountcode));
	sccp_copy_string(l->musicclass, GLOB(musicclass), sizeof(l->musicclass));
	l->amaflags = GLOB(amaflags);
	l->callgroup = GLOB(callgroup);
#ifdef CS_SCCP_PICKUP
	l->pickupgroup = GLOB(pickupgroup);
#endif
	return l;
}

/*!
 * \brief Kill all Channels of a specific Line
 * \param l SCCP Line
 * \note Should be Called with a lock on l->lock
 */
void sccp_line_kill(sccp_line_t * l)
{
	sccp_channel_t * c;

	if (!l)
		return;

	SCCP_LIST_LOCK(&l->channels);
	SCCP_LIST_TRAVERSE(&l->channels, c, list) {
		sccp_channel_endcall(c);
	}
	SCCP_LIST_UNLOCK(&l->channels);
}

/*!
 * \brief Delete an SCCP line
 * \param l SCCP Line
 * \note Should be Called without a lock on l->lock
 */
void sccp_line_delete_nolock(sccp_line_t * l)
{

	sccp_device_t 		*d;
	sccp_linedevices_t	*linedevice;

	if (!l)
		return;

	sccp_line_kill(l);

	//d = l->device;

	SCCP_LIST_LOCK(&l->devices);
	while( (linedevice = SCCP_LIST_REMOVE_HEAD(&l->devices, list))){
		if(linedevice) {
			if(!linedevice->device)
				continue;
			d = linedevice->device;

			/* remove the line from the device lines list */
			//SCCP_LIST_LOCK(&d->lines);
			//SCCP_LIST_REMOVE(&d->lines, l, listperdevice);
			//SCCP_LIST_UNLOCK(&d->lines);
			sccp_device_lock(d);
			d->linesCount--;
			sccp_device_unlock(d);

			ast_free(linedevice);
		}
	}
	SCCP_LIST_UNLOCK(&l->devices);

	/* remove from the global lines list */
	SCCP_LIST_LOCK(&GLOB(lines));
	SCCP_LIST_REMOVE(&GLOB(lines), l, list);
	SCCP_LIST_LOCK(&GLOB(lines));

	if (l->cfwd_num)
		ast_free(l->cfwd_num);
	if (l->trnsfvm)
		ast_free(l->trnsfvm);

	sccp_mutex_destroy(&l->lock);

	sccp_mailbox_t *mailbox = NULL;

	while( (mailbox = SCCP_LIST_REMOVE_HEAD(&l->mailboxes, list))){
		if(!mailbox)
			return;

		sccp_mwi_unsubscribeMailbox(&mailbox);
		if (mailbox->mailbox)
			sccp_free(mailbox->mailbox);
		if (mailbox->context)
			sccp_free(mailbox->context);
		sccp_free(mailbox);
	}



	ast_free(l);
}

/*!
 * \brief Set a Call Forward on a specific Line
 * \param l SCCP Line
 * \param type Call Forward Type as uint8_t
 * \param number Number to which should be forwarded
 * \todo we should check, that extension is reachable on line
 */
void sccp_line_cfwd(sccp_line_t * l, uint8_t type, char * number) {
	sccp_linedevices_t *linedevice;

	if (!l)
		return;


	if (l->cfwd_num) {
		ast_free(l->cfwd_num);
		l->cfwd_num = NULL;
	}

	if (type == SCCP_CFWD_NONE) {
		l->cfwd_type = SCCP_CFWD_NONE;
		sccp_log(1)(VERBOSE_PREFIX_3 "%s: Call Forward disabled on line\n", l->name);
	} else {
		if (!number || ast_strlen_zero(number)) {
			l->cfwd_type = SCCP_CFWD_NONE;
			sccp_log(1)(VERBOSE_PREFIX_3 "%s: Call Forward to an empty number. Invalid\n", l->name);
		}else{
			l->cfwd_num = strdup(number);
			l->cfwd_type = type;
			sccp_log(1)(VERBOSE_PREFIX_3 "%s: Call Forward enabled on line %s to number %s\n", l->name, l->name, number);
		}
	}
	// sccp_dev_starttone(d, SKINNY_TONE_ZIPZIP, l->instance, 0, 0);

	SCCP_LIST_TRAVERSE(&l->devices, linedevice, list){
		if(linedevice && linedevice->device){
			sccp_dev_forward_status(l, linedevice->device);
			sccp_dev_dbput(linedevice->device);
			sccp_dev_starttone(linedevice->device, SKINNY_TONE_ZIPZIP, 0, 0, 0);


			sccp_feat_changed(linedevice->device, SCCP_FEATURE_CFWDALL);
		}
	}
}

/*!
 * \brief Attach a Device to a line
 * \param l SCCP Line
 * \param device SCCP Device
 */
void sccp_line_addDevice(sccp_line_t * l, sccp_device_t *device)
{
        sccp_linedevices_t *linedevice;
	if(!l || !device)
		return;

	sccp_log(64)(VERBOSE_PREFIX_3 "%s: add device to line %s\n", DEV_ID_LOG(device), l->name);
	linedevice = ast_malloc(sizeof(sccp_linedevices_t));
	memset(linedevice, 0, sizeof(sccp_linedevices_t));
	linedevice->device = device;

	SCCP_LIST_LOCK(&l->devices);
	SCCP_LIST_INSERT_HEAD(&l->devices, linedevice, list);
	SCCP_LIST_UNLOCK(&l->devices);

	sccp_line_lock(l);
	l->statistic.numberOfActiveDevices++;
	sccp_line_unlock(l);


	// fire event for new device
	sccp_event_t *event =ast_malloc(sizeof(sccp_event_t));
	memset(event, 0, sizeof(sccp_event_t));

	event->type=SCCP_EVENT_DEVICEATTACHED;
	event->event.deviceAttached.line = l;
	event->event.deviceAttached.device = device;
	sccp_event_fire((const sccp_event_t**)&event);
}


/*!
 * \brief Remove a Device from a Line
 *
 * Fire SCCP_EVENT_DEVICEDETACHED event after removing device.
 *
 * \param l SCCP Line
 * \param device SCCP Device
 */
void sccp_line_removeDevice(sccp_line_t * l, sccp_device_t *device)
{
	sccp_linedevices_t *linedevice;

	if(!l || !device)
		return;

	sccp_log(64)(VERBOSE_PREFIX_3 "%s: remove device from line %s\n", DEV_ID_LOG(device), l->name);
	SCCP_LIST_LOCK(&l->devices);
	SCCP_LIST_TRAVERSE(&l->devices, linedevice, list) {
		if (linedevice->device == device) {
			SCCP_LIST_LOCK(&l->devices);
			SCCP_LIST_REMOVE(&l->devices, linedevice, list);
			SCCP_LIST_UNLOCK(&l->devices);

			sccp_line_lock(l);
			l->statistic.numberOfActiveDevices--;
			sccp_line_unlock(l);
			ast_free(linedevice);
			break;
		}
	}
	SCCP_LIST_UNLOCK(&l->devices);
	sccp_event_t *event =ast_malloc(sizeof(sccp_event_t));
	memset(event, 0, sizeof(sccp_event_t));

	event->type=SCCP_EVENT_DEVICEDETACHED;
	event->event.deviceAttached.line = l;
	event->event.deviceAttached.device = device;
	sccp_event_fire((const sccp_event_t**)&event);
}


/*!
 * \brief Add a Channel to a Line
 *
 * \param l SCCP Line
 * \param channel SCCP Channel
 */
void sccp_line_addChannel(sccp_line_t * l, sccp_channel_t *channel)
{
	if(!l || !channel)
		return;

	sccp_line_lock(l);
	l->channelCount++;
	l->statistic.numberOfActiveChannels++;
	sccp_line_unlock(l);

	if(GLOB(callAnswerOrder) == ANSWER_OLDEST_FIRST)
		SCCP_LIST_INSERT_TAIL(&l->channels, channel, list);
	else
		SCCP_LIST_INSERT_HEAD(&l->channels, channel, list);
}

/*!
 * \brief Remove a Channel from a Line
 *
 * \param l SCCP Line
 * \param channel SCCP Channel
 */
void sccp_line_removeChannel(sccp_line_t * l, sccp_channel_t *channel)
{
	if(!l || !channel)
		return;

	sccp_line_lock(l);
	l->channelCount--;
	sccp_line_unlock(l);

	SCCP_LIST_REMOVE(&l->channels, channel, list);
}

