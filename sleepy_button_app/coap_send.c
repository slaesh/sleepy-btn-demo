#include <openthread/config.h>
#include <openthread-core-config.h>

/* Standard Library Header files */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* POSIX Header files */
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <mqueue.h>

/* RTOS Header files */
#include <ti/drivers/GPIO.h>

/* OpenThread public API Header files */
#include <openthread/dataset.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/uart.h>
#include <openthread/tasklet.h>
#include <openthread/thread.h>

#include <openthread/coap.h>

/* OpenThread Internal/Example Header files */
#include "otsupport/otrtosapi.h"
#include "otsupport/otinstance.h"

/* Example/Board Header files */
#include "ti_drivers_config.h"

#include "otstack.h"
#include "utils/code_utils.h"

/* Private configuration Header files */
#include "task_config.h"
#include "tiop_config.h"

#include "myuart.h"
#include "json_stuff.h"
#include "oad_image_header.h"
#include "inputs.h"
#include "movement-sensor.h"

extern otExtAddress ourEUI64Address;

char serializedJsonBuf[1024];
Json_Handle hTemplate;
Json_Handle hObject;

static bool doOnce = true;

extern uint32_t batteryVoltage;
extern int32_t celsiusTemp;

void coapSendState(char *topic, bool retain)
{
    otError error = OT_ERROR_NONE;
    otInstance *instance = OtInstance_get();

    OtRtosApi_lock();
    int32_t role = otThreadGetDeviceRole(instance);
    OtRtosApi_unlock();

    static char msg[70];
    if (role <= OT_DEVICE_ROLE_DETACHED)
    {
        snprintf(msg, sizeof(msg), "invalid role: %d", role);
        myuart_log(msg);

        return;
    }

    uint16_t valueSize;
    int16_t retVal;

    if (doOnce == true)
    {
        topic = "sleepy_btn/started";
        doOnce = false;
    }

    retVal = Json_createTemplate(&hTemplate, JSON_PAYLOAD_TEMPLATE,
                                 strlen(JSON_PAYLOAD_TEMPLATE));
    if (retVal != JSON_RC__OK)
    {
        myuart_log("cant create template");
        goto exit;
    }

    retVal = Json_createObject(&hObject, hTemplate, 256);
    if (retVal != JSON_RC__OK)
    {
        myuart_log("cant create obj");
        goto exit;
    }

    // set sw version
    retVal = Json_setValue(hObject, JSON_PAYLOAD_SWVER_PROPERTY,
                           oad_image_header.h.softVer, 4);

    // set EUI64
    static char euid[20];
    snprintf(euid, sizeof(euid), "%02x%02x%02x%02x%02x%02x%02x%02x",
             ourEUI64Address.m8[0], ourEUI64Address.m8[1],
             ourEUI64Address.m8[2], ourEUI64Address.m8[3],
             ourEUI64Address.m8[4], ourEUI64Address.m8[5],
             ourEUI64Address.m8[6], ourEUI64Address.m8[7]);

    retVal = Json_setValue(hObject, JSON_PAYLOAD_EUID_PROPERTY, euid,
                           strlen(euid));
    if (retVal != JSON_RC__OK)
    {
        myuart_log("cant set val");
        goto exit;
    }

    valueSize = sizeof(celsiusTemp);
    Json_setValue(hObject, JSON_PAYLOAD_TEMPERATURE_PROPERTY, &celsiusTemp,
                  valueSize);

    valueSize = sizeof(batteryVoltage);
    Json_setValue(hObject, JSON_PAYLOAD_BATTERYVOLTAGE_PROPERTY,
                  &batteryVoltage, valueSize);

    // build json
    uint16_t jsonBufSize = sizeof(serializedJsonBuf);
    retVal = Json_build(hObject, serializedJsonBuf, &jsonBufSize);
    if (retVal != JSON_RC__OK)
    {
        snprintf(msg, sizeof(msg), "cant build: %d .. %d", retVal, jsonBufSize);
        myuart_log(msg);

        snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                 "serializedJsonBuf: err!");
        goto exit;
    }

    // ot msg stuff..

    otMessage *requestMessage;
    otMessageInfo messageInfo;

    OtRtosApi_lock();
    requestMessage = otCoapNewMessage(instance, NULL);
    OtRtosApi_unlock();
    otEXPECT_ACTION(requestMessage != NULL, error = OT_ERROR_NO_BUFS);

    OtRtosApi_lock();
    otCoapMessageInit(requestMessage, OT_COAP_TYPE_NON_CONFIRMABLE,
                      OT_COAP_CODE_POST);
#define DEFAULT_COAP_HEADER_TOKEN_LEN (2)
    otCoapMessageGenerateToken(requestMessage, DEFAULT_COAP_HEADER_TOKEN_LEN);
    error = otCoapMessageAppendUriPathOptions(requestMessage, topic);
    OtRtosApi_unlock();
    otEXPECT(OT_ERROR_NONE == error);

    OtRtosApi_lock();
    otCoapMessageSetPayloadMarker(requestMessage);
    error = otMessageAppend(requestMessage, serializedJsonBuf,
                            strlen((const char*) serializedJsonBuf));
    OtRtosApi_unlock();
    otEXPECT(OT_ERROR_NONE == error);

    /* IPv6 address to send the reporting temperature to */

    // ff03::1   Mesh-Local  Alle FTDs und MEDs
    // ff03::2   Mesh-Local  Alle FTDs
    static otIp6Address reportingAddress = { .mFields.m8 = { 0xff, 0x03, 0x00,
                                                             0x00, 0x00, 0x00,
                                                             0x00, 0x00, 0x00,
                                                             0x00, 0x00, 0x00,
                                                             0x00, 0x00, 0x00,
                                                             0x02 }, };

    OtRtosApi_lock(); // <-- this was initially not here? but in "oad_sendRegMsg" its done, too!
    const otIp6Address *ipAddr_p = otThreadGetMeshLocalEid(OtInstance_get());
    OtRtosApi_unlock(); // <-- this was initially not here? but in "oad_sendRegMsg" its done, too!

    memset(&messageInfo, 0, sizeof(messageInfo));

    messageInfo.mSockAddr = *ipAddr_p;
    messageInfo.mSockPort = OT_DEFAULT_COAP_PORT;

    messageInfo.mPeerAddr = reportingAddress;
    messageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

    OtRtosApi_lock();
    error = otCoapSendRequest(instance, requestMessage, &messageInfo, NULL,
                              NULL);
    OtRtosApi_unlock();

    exit:

    retVal = Json_destroyObject(hObject);
    if (retVal != JSON_RC__OK)
    {
        myuart_log("cant destroy obj");
    }

    retVal = Json_destroyTemplate(hTemplate);
    if (retVal != JSON_RC__OK)
    {
        myuart_log("cant destroy temp");
    }

    if (error != OT_ERROR_NONE && requestMessage != NULL)
    {
        myuart_log("free..");

        OtRtosApi_lock();
        otMessageFree(requestMessage);
        OtRtosApi_unlock();
    }
}
