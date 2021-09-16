#ifndef JSON_STUFF_H_
#define JSON_STUFF_H_

#include <ti/utils/json/json.h>

#define JSON_PAYLOAD_HWREV_PROPERTY "\"hw_rev\""
#define JSON_PAYLOAD_SWVER_PROPERTY "\"sw_ver\""

#define JSON_PAYLOAD_TEMPERATURE_PROPERTY "\"t\""
#define JSON_PAYLOAD_EUID_PROPERTY "\"uid\""
#define JSON_PAYLOAD_BATTERYVOLTAGE_PROPERTY "\"b\""

// only int32 works for numbers?!
#define JSON_PAYLOAD_TEMPLATE                   \
"{"                                             \
    JSON_PAYLOAD_EUID_PROPERTY":string,"    \
    JSON_PAYLOAD_SWVER_PROPERTY":string,"    \
    JSON_PAYLOAD_BATTERYVOLTAGE_PROPERTY":int32,"          \
    JSON_PAYLOAD_TEMPERATURE_PROPERTY":int32,"          \
"}"

// only int32 works for numbers?!
#define JSON_GET_UID_TEMPLATE                   \
"{"                                             \
    JSON_PAYLOAD_EUID_PROPERTY":string"        \
"}"

#endif /* JSON_STUFF_H_ */
