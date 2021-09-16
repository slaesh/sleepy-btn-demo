/******************************************************************************

 @file doorlock.c

 @brief Door lock example application

 Group: CMCU, LPC
 Target Device: cc13x2_26x2

 ******************************************************************************
 
 Copyright (c) 2017-2021, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
 its contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

/******************************************************************************
 Includes
 *****************************************************************************/
#include <openthread/config.h>
#include <openthread-core-config.h>

/* Standard Library Header files */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* OpenThread public API Header files */
#include <openthread/coap.h>
#include <openthread/dataset.h>
#include <openthread/platform/uart.h>
#include <openthread/tasklet.h>
#include <openthread/thread.h>

/* POSIX Header files */
#include <sched.h>
#include <pthread.h>
#include <mqueue.h>

/* RTOS Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/apps/Button.h>
#include <ti/drivers/apps/LED.h>

#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>

/* OpenThread Internal/Example Header files */
#include "otsupport/otrtosapi.h"
#include "otsupport/otinstance.h"

/* Board Header files */
#include "ti_drivers_config.h"

#include "doorlock.h"
#include "utils/code_utils.h"
#include "otstack.h"

/* OAD required Header files */
#include "oad/oad.h"
#include "oad_image_header.h"
/* Low level driverlib files (non-rtos) */
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/flash.h)
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h)
#include DeviceFamily_constructPath(driverlib/cpu.h)

/* Private configuration Header files */
#include "task_config.h"
#include "tiop_config.h"

#include "coap_server.h"
#include "coap_send.h"
#include "myuart.h"
#include "timer.h"
#include "inputs.h"
#include <driverlib/aon_batmon.h>

#if (OPENTHREAD_CONFIG_COAP_API_ENABLE == 0)
#error "OPENTHREAD_CONFIG_COAP_API_ENABLE needs to be defined and set to 1"
#endif

#define DOORLOCK_PROC_QUEUE_MAX_MSG     (22)

/* coap attribute descriptor */
typedef struct
{
    const char *uriPath; /* attribute URI */
    uint16_t type; /* type of resource: read only or read write */
    uint8_t *pValue; /* pointer to value of attribute state */
} attrDesc_t;

struct Doorlock_procQueueMsg
{
    appEvent_e evt;
};

/* POSIX message queue for passing events to the application processing loop. */
const char Doorlock_procQueueName[] = "/dl_process";
static mqd_t Doorlock_procQueueDesc;

static char stack[TASK_CONFIG_DOORLOCK_TASK_STACK_SIZE];

static timer_t reportingTimer;
static void reportingTimerCb(union sigval val)
{
    app_postEvt(APP_reportState);

    (void) val;
}

void triggerReportingTimer()
{
    union sigval val;
    reportingTimerCb(val);
}

static timer_t tryJoinTimer;
static void tryJoinTimerCb(union sigval val)
{
    app_postEvt(APP_tryJoinNetwork);

    (void) val;
}

static timer_t btnDebounceTimer;
static void btnDebounceTimerCb(union sigval val)
{
    app_postEvt(APP_evtBtnClickDebounced);

    (void) val;
}

void* DoorLock_task(void *arg0);

static void processOtStackEvents(uint8_t event, void *aContext)
{
    (void) aContext;

    switch (event)
    {
    case OT_STACK_EVENT_NWK_JOINED:
    {
        app_postEvt(DoorLock_evtNwkJoined);
        break;
    }

    case OT_STACK_EVENT_NWK_JOINED_FAILURE:
    {
        app_postEvt(DoorLock_evtNwkJoinFailure);
        break;
    }

    case OT_STACK_EVENT_NWK_DATA_CHANGED:
    {
        app_postEvt(DoorLock_evtNwkSetup);
        break;
    }

    case OT_STACK_EVENT_DEV_ROLE_CHANGED:
    {
        app_postEvt(DoorLock_evtDevRoleChanged);
        break;
    }

    default:
    {
        /* do nothing */
        break;
    }
    }
}

bool joined = false;
bool btnAlreadyClicked = false;
uint16_t btnDownCnt = 0;

static void processEvent(appEvent_e event)
{
    switch (event)
    {

    case DoorLock_evtNwkSetup:
    {
        setupCoapServer(OtInstance_get());

        break;
    }

    case APP_tryJoinNetwork:
    {
        myuart_log("try2join");

        if (joined == false)
        {
            OtRtosApi_lock();
            bool isCommisioned = otDatasetIsCommissioned(OtInstance_get());
            OtRtosApi_unlock();

            uint8_t joinState = OtStack_joinState();

            if (!isCommisioned
                    && (joinState != OT_STACK_EVENT_NWK_JOIN_IN_PROGRESS))
            {
                myuart_log("Joining Nwk ...");
                OtStack_joinConfiguredNetwork();
            }
        }
        else
        {

            myuart_log("=> already joined!");

        }

        break;
    }

    case DoorLock_evtNwkJoined:
    {
        myuart_log("Joined Nwk");

        (void) OtStack_setupNetwork();

        break;
    }

    case DoorLock_evtNwkJoinFailure:
    {
        myuart_log("Join Failure");

        timer_start(tryJoinTimer, 10000);

        break;
    }

    case DoorLock_evtDevRoleChanged:
    {
        OtRtosApi_lock();
        otDeviceRole role = otThreadGetDeviceRole(OtInstance_get());
        OtRtosApi_unlock();

        switch (role)
        {
        case OT_DEVICE_ROLE_DISABLED:
        {
            /* fall through */
        }

        case OT_DEVICE_ROLE_DETACHED:
        {
            myuart_log("Device Detached");

            OAD_pause();

            joined = false;

            timer_start(tryJoinTimer, 10000);

            break;
        }

        case OT_DEVICE_ROLE_CHILD:
        case OT_DEVICE_ROLE_ROUTER:
        case OT_DEVICE_ROLE_LEADER:
        {
            joined = true;

            myuart_log("Device Joined");

            OAD_resume();

            triggerReportingTimer();

            break;
        }
        }

        break;
    }

    case OAD_queueEvt:
    {
        OAD_processQueue();
        break;
    }

    case OAD_CtrlRegEvt:
    {
        /* perform activity related to the OAD Download. */
        OAD_processCtrlEvents(event);
        break;
    }

    case APP_evtInputChanged:
    {
        myuart_log("click");

        if (joined == true)
        {

            // only send the first time..
            if (btnAlreadyClicked == false)
            {
                coapSendState("sleepy_btn/click", false);
            }
            else
            {
                myuart_log(".. ignore!");
            }

        }
        else
        {

            timer_start(tryJoinTimer, 300);
        }

        // we do not "debounce" .. we just start a timer to NOT send any other clicks afterwards..
        // why? to NOT waste time while debouncing.. lets just use the first occurance! ;)
        btnAlreadyClicked = true;
        timer_start(btnDebounceTimer, 300);

        break;
    }

    case APP_evtBtnClickDebounced:
    {
        btnAlreadyClicked = false;

        PIN_Id input0 = inputs_getPinIdByInputNo(0);
        uint8_t input0pinState = PIN_getInputValue(input0);

        // still pressed, check for a long press!
        if (input0pinState == 0)
        {
            if (++btnDownCnt >= 20)
            {
                myuart_log("longpress");
                btnDownCnt = 0;

                OtRtosApi_lock();
                otInstanceFactoryReset(OtInstance_get());
                OtRtosApi_unlock();

            }
            else
            {

                myuart_log("#");

                timer_start(btnDebounceTimer, 100);
            }
        }
        // just debounced.. reset stuff!
        else
        {
            myuart_log("debounced");

            btnDownCnt = 0;
        }

        break;
    }

    case APP_reportState:
    {
        myuart_log("report");

        coapSendState("sleepy_btn/state", false);

        timer_start(reportingTimer, 60000 * 5);

        break;
    }

    }
}

/* Documented in doorlock.h */
void app_postEvt(appEvent_e event)
{
    struct Doorlock_procQueueMsg msg;
    int ret;
    msg.evt = event;
    ret = mq_send(Doorlock_procQueueDesc, (const char*) &msg, sizeof(msg), 0);
    assert(0 == ret);
    (void) ret;
}

/**
 * Documented in task_config.h.
 */
void DoorLock_taskCreate(void)
{
    pthread_t thread;
    pthread_attr_t pAttrs;
    struct sched_param priParam;
    int retc;

    retc = pthread_attr_init(&pAttrs);
    assert(retc == 0);

    retc = pthread_attr_setdetachstate(&pAttrs,
    PTHREAD_CREATE_DETACHED);
    assert(retc == 0);

    priParam.sched_priority =
    TASK_CONFIG_DOORLOCK_TASK_PRIORITY;
    retc = pthread_attr_setschedparam(&pAttrs, &priParam);
    assert(retc == 0);

    retc = pthread_attr_setstack(&pAttrs, (void*) stack,
    TASK_CONFIG_DOORLOCK_TASK_STACK_SIZE);
    assert(retc == 0);

    retc = pthread_create(&thread, &pAttrs, DoorLock_task, NULL);
    assert(retc == 0);

    retc = pthread_attr_destroy(&pAttrs);
    assert(retc == 0);

    (void) retc;

}

/**
 * @brief Invalidate the OAD IMAGE HEADER.
 *
 * This provides a way to revert to factory image by invalidating the existing
 * stack/application image and doing a system reset. On boot, the BIM
 * on finding the internal invalidated image will restore to factory image.
 *
 * @return None
 */
static void TIOP_OAD_invalidate_image_header(void)
{
    /* our data buffer cannot be in flash... so it is on the stack */
    uint8_t zeros[sizeof(oad_image_header.h.imgID)];
    /*
     * We only need to invalidate the IMAGE header
     * We can do this by writing a zero  "zeros" over the signature.
     */

    /* no IRQ chance, we disable here */
    CPUcpsid();

    memset(zeros, 0, sizeof(zeros));
    FlashProgram(&zeros[0], (uint32_t) (&oad_image_header.h.imgID),
                 sizeof(oad_image_header.h.imgID));

    /* press the virtual reset button */
    SysCtrlSystemReset();
}

otExtAddress ourEUI64Address;

uint32_t batteryVoltage = 0;
int32_t celsiusTemp = 0;

void* DoorLock_task(void *arg0)
{
    struct mq_attr attr;
#if !TIOP_CONFIG_SET_NW_ID
    bool commissioned;
#endif /* !TIOP_CONFIG_SET_NW_ID */
    mqd_t procQueueLoopDesc;

    attr.mq_curmsgs = 0;
    attr.mq_flags = 0;
    attr.mq_maxmsg = DOORLOCK_PROC_QUEUE_MAX_MSG;
    attr.mq_msgsize = sizeof(struct Doorlock_procQueueMsg);

    /* Open The processing queue in non-blocking write mode for the notify
     * callback functions
     */
    Doorlock_procQueueDesc = mq_open(Doorlock_procQueueName,
                                     (O_WRONLY | O_NONBLOCK | O_CREAT), 0,
                                     &attr);

    /* Open the processing queue in blocking read mode for the process loop */
    procQueueLoopDesc = mq_open(Doorlock_procQueueName,
    O_RDONLY,
                                0, NULL);

    timer_configure(&reportingTimer, reportingTimerCb);
    timer_start(reportingTimer, 15000);

    timer_configure(&tryJoinTimer, tryJoinTimerCb);
    timer_start(tryJoinTimer, 5000);

    timer_configure(&btnDebounceTimer, btnDebounceTimerCb);

    OtStack_taskCreate();

    OtStack_registerCallback(processOtStackEvents);

    PIN_Handle hInputs = inputs_init();

    // enable interrupt, falling edge..
    PIN_setInterrupt(hInputs, inputs_getPinIdByInputNo(0) | PIN_IRQ_NEGEDGE);

    /* If button 2 is pressed on boot, reset the OpenThread settings */
    // => we need this in any way?!
    OtRtosApi_lock();
    commissioned = otDatasetIsCommissioned(OtInstance_get());
    OtRtosApi_unlock();

#if false
    PIN_Id input1 = inputs_getPinIdByInputNo(1);
    uint8_t input1pinState = PIN_getInputValue(input1);

    if (commissioned && input1pinState == 0)
    {
        /*
         otOperationalDataset aDataset;

         OtRtosApi_lock();
         otDatasetGetActive(OtInstance_get(), &aDataset);
         OtRtosApi_unlock();

         if (strncmp(aDataset.mNetworkName.m8, "OpenThread",
         OT_NETWORK_NAME_MAX_SIZE) != 0)
         {
         */

        uint8_t cnt = 0;

        while (++cnt <= 50)
        {
            GPIO_toggle(CONFIG_GPIO_GLED);
            GPIO_toggle(CONFIG_GPIO_RLED);
            Task_sleep(25 * 1000 / Clock_tickPeriod);
        }

        OtRtosApi_lock();
        otInstanceFactoryReset(OtInstance_get());
        OtRtosApi_unlock();

        //}
    }
#endif

    myuart_log("sleepy button init!");

    OtRtosApi_lock();
    otLinkGetFactoryAssignedIeeeEui64(OtInstance_get(), &ourEUI64Address);
    OtRtosApi_unlock();

#if !TIOP_CONFIG_SET_NW_ID
    /*
     OtRtosApi_lock();
     commissioned = otDatasetIsCommissioned(OtInstance_get());
     OtRtosApi_unlock();
     */

    if (false == commissioned)
    {
        /*
         myuart_log("pskd: %s", TIOP_CONFIG_PSKD);
         myuart_log("EUI64: 0x%02x%02x%02x%02x%02x%02x%02x%02x",
         extAddress.m8[0], extAddress.m8[1], extAddress.m8[2],
         extAddress.m8[3], extAddress.m8[4], extAddress.m8[5],
         extAddress.m8[6], extAddress.m8[7]);
         */

    }
    else
    {
        OtStack_setupInterfaceAndNetwork();
    }
#else

    OtStack_setupInterfaceAndNetwork();
#endif /* !TIOP_CONFIG_SET_NW_ID */

    AONBatMonEnable();

    /* process events */
    while (1)
    {
        struct Doorlock_procQueueMsg msg;
        ssize_t ret;

        ret = mq_receive(procQueueLoopDesc, (char*) &msg, sizeof(msg), NULL);
        /* priorities are ignored */
        if (ret < 0 || ret != sizeof(msg))
        {
            /* there was an error on receive or we did not receive a full message */
            continue;
        }

        /* make sure there is a new temperature reading otherwise just report the previous temperature */
        if (AONBatMonNewTempMeasureReady())
        {
            /* Read the temperature in degrees C from the internal temp sensor */
            celsiusTemp = AONBatMonTemperatureGetDegC();
        }

        if (AONBatMonNewBatteryMeasureReady())
        {
            batteryVoltage = AONBatMonBatteryVoltageGet();
            // batteryVoltage = (batteryVoltage * 1000) >> AON_BATMON_BAT_FRAC_W
        }

        processEvent(msg.evt);
    }
}
