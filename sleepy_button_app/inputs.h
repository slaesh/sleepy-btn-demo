/*
 * inputs.h
 *
 *  Created on: May 14, 2021
 *      Author: sascha
 */

#ifndef INPUTS_H_
#define INPUTS_H_

#include <ti/drivers/PIN.h>

extern PIN_Handle inputs_init();
extern PIN_Handle inputs_getHandle();
extern PIN_Id inputs_getPinIdByInputNo(uint8_t inputNo);

#endif /* INPUTS_H_ */
