#define _COAP_SERVER_C_

#include "coap_server.h"

/* OpenThread Internal/Example Header files */
#include "otsupport/otrtosapi.h"
#include "otsupport/otinstance.h"
#include "utils/code_utils.h"
#include <stdio.h>

#include "oad/oad.h"
#include "oad_image_header.h"
#include "myuart.h"

#include "json_stuff.h"
#include "doorlock.h"

extern otExtAddress ourEUI64Address;

static otCoapResource coapGetEui64Resource;
static otCoapResource coapSetOutputResource;

bool coapServerSetup = false;

static void coapHandleGetEui64(void *aContext, otMessage *aMessage,
                               const otMessageInfo *aMessageInfo)
{
    myuart_log("coapHandleGetEui64");

    if (coapServerSetup == false)
    {
        myuart_log("not initialized yet!");
        return;
    }

    otError error = OT_ERROR_NONE;
    otMessage *responseMessage;
    otCoapCode messageCode = otCoapMessageGetCode(aMessage);
    otCoapType messageType = otCoapMessageGetType(aMessage);

    responseMessage = otCoapNewMessage((otInstance*) aContext, NULL);
    otEXPECT_ACTION(responseMessage != NULL, error = OT_ERROR_NO_BUFS);

    otCoapMessageInitResponse(responseMessage, aMessage,
                              OT_COAP_TYPE_ACKNOWLEDGMENT,
                              OT_COAP_CODE_CHANGED);
    otCoapMessageSetToken(responseMessage, otCoapMessageGetToken(aMessage),
                          otCoapMessageGetTokenLength(aMessage));
    otCoapMessageSetPayloadMarker(responseMessage);

    char serializedJsonBuf[1024];
    Json_Handle hTemplate = (Json_Handle) NULL;
    Json_Handle hObject = (Json_Handle) NULL;
    int32_t retVal = 0;

    if (OT_COAP_CODE_GET == messageCode)
    {
        myuart_log("create template");
        retVal = Json_createTemplate(&hTemplate,
        JSON_GET_UID_TEMPLATE,
                                     strlen(JSON_GET_UID_TEMPLATE));

        if (retVal != JSON_RC__OK)
        {
            snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                     "err! cannot create template: %d", retVal);
            myuart_log(serializedJsonBuf);

            goto exit;
        }

        myuart_log("create object");
        retVal = Json_createObject(&hObject, hTemplate, 128);
        if (retVal != JSON_RC__OK)
        {
            snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                     "err! cannot create obj: %d", retVal);
            myuart_log(serializedJsonBuf);

            goto exit;
        }

        char euid[20];
        snprintf(euid, sizeof(euid), "%02x%02x%02x%02x%02x%02x%02x%02x",
                 ourEUI64Address.m8[0], ourEUI64Address.m8[1],
                 ourEUI64Address.m8[2], ourEUI64Address.m8[3],
                 ourEUI64Address.m8[4], ourEUI64Address.m8[5],
                 ourEUI64Address.m8[6], ourEUI64Address.m8[7]);

        myuart_log(euid);

        static char msg[20];
        snprintf(msg, sizeof(msg), "%d %d", strlen(euid), hObject);
        myuart_log(msg);

        retVal = Json_setValue(hObject, JSON_PAYLOAD_EUID_PROPERTY, euid,
                               strlen(euid));
        if (retVal != JSON_RC__OK)
        {
            snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                     "err! cannot set value: %d", retVal);
            myuart_log(serializedJsonBuf);

            goto exit;
        }

        uint16_t jsonBufSize = sizeof(serializedJsonBuf);
        retVal = Json_build(hObject, serializedJsonBuf, &jsonBufSize);
        if (retVal != JSON_RC__OK)
        {
            snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                     "err! cannot build: %d", retVal);

            goto exit;
        }

        myuart_log(serializedJsonBuf);

        error = otMessageAppend(responseMessage, serializedJsonBuf,
                                strlen(serializedJsonBuf));
        otEXPECT(OT_ERROR_NONE == error);

        error = otCoapSendResponse((otInstance*) aContext, responseMessage,
                                   aMessageInfo);
        otEXPECT(OT_ERROR_NONE == error);
    }

    exit:

    retVal = Json_destroyTemplate(hTemplate);
    if (retVal != JSON_RC__OK)
    {
        snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                 "err! cannot destroyT: %d", retVal);
        myuart_log(serializedJsonBuf);
    }

    retVal = Json_destroyObject(hObject);
    if (retVal != JSON_RC__OK)
    {
        snprintf(serializedJsonBuf, sizeof(serializedJsonBuf),
                 "err! cannot destroyO: %d", retVal);
        myuart_log(serializedJsonBuf);
    }

    if (error != OT_ERROR_NONE && responseMessage != NULL)
    {
        otMessageFree(responseMessage);
    }
}

void setUpRoutes(otInstance *aInstance)
{
    coapGetEui64Resource.mHandler = &coapHandleGetEui64;
    coapGetEui64Resource.mUriPath = "get/eui64";
    coapGetEui64Resource.mContext = aInstance;

    OtRtosApi_lock();
    otCoapRemoveResource(aInstance, &coapGetEui64Resource);
    /* error = */otCoapAddResource(aInstance, &coapGetEui64Resource);
    OtRtosApi_unlock();

}

otError setupCoapServer(otInstance *aInstance)
{
    otError error = OT_ERROR_NONE;

    if (coapServerSetup == true)
    {
        myuart_log("already initialized..!");
        return OT_ERROR_ALREADY;
    }

    OtRtosApi_lock();
    error = otCoapStop(aInstance);
    error = otCoapStart(aInstance, OT_DEFAULT_COAP_PORT);
    OtRtosApi_unlock();

    otEXPECT(OT_ERROR_NONE == error);

    setUpRoutes(aInstance);

    /*setup OAD CoAP resources */
    OAD_open();

    if (!coapServerSetup)
    {
        myuart_log("coap server up and running..!");
    }

    coapServerSetup = true;

    exit: return error;
}
