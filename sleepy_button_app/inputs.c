/*
 * inputs.c
 *
 *  Created on: May 14, 2021
 *      Author: sascha
 */

#include <stddef.h>
#include <stdio.h>

#include "inputs.h"
#include "doorlock.h"
#include "myuart.h"

// DO NOT USE THE BOOTLOADER PIN !
static const PIN_Config inputPinCfg[] = { 14 | PIN_INPUT_EN | PIN_PULLUP, /**/
                                          PIN_TERMINATE /* Terminate list */
};
static PIN_Handle hInputPins = NULL;

void inputs_irq(PIN_Handle handle, PIN_Id pinId)
{
    app_postEvt(APP_evtInputChanged);
}

PIN_Handle inputs_init()
{
    static PIN_State inputPinState;

    hInputPins = PIN_open(&inputPinState, inputPinCfg);

    if (hInputPins == NULL)
    {
        myuart_log("inputs_init(): failed!");
        return NULL;
    }

    PIN_registerIntCb(hInputPins, inputs_irq);

    myuart_log("inputs_init(): done!");

    return hInputPins;
}

PIN_Handle inputs_getHandle()
{
    return hInputPins;
}

PIN_Id inputs_getPinIdByInputNo(uint8_t inputNo)
{
    if (inputNo >= sizeof(inputPinCfg) / sizeof(PIN_Config))
        return PIN_TERMINATE;

    return PIN_ID(inputPinCfg[inputNo]);
}
