#ifndef BGM_LAYOUT_H
#define BGM_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
#define BGM_MAGIC 0x31454742u
#define BGM_VERSION 2u
#define BGM_API_COUNT 34u
#define BGM_MAX_RULES 32u
#define BGM_TIMEOUT_MS 5000u
enum { BGM_HAS_BUFF, BGM_START_STACK_EQ, BGM_START_NO_BUFF, BGM_END_TRANSITION };
typedef struct {
 int32_t personality, priority;
 char keyword[64], path[128], id[48], bank[64];
 int32_t condition,threshold;
 char from_keyword[64];
} BGM_RULE;
typedef struct {
 uint32_t magic,version,header_size;
 volatile uint32_t enabled,heartbeat,hooked;
 volatile int32_t error,mh_status;
 volatile uint32_t updates,changes,blocked,locked;
 volatile int32_t priority,active_rule,phase;
 volatile uint32_t sequence,rule_count,generation;
 uint32_t reserved0[2];
 char message[256],active_bgm[128];
 uint32_t reserved1[12];
 char aliases[BGM_API_COUNT][64];
 BGM_RULE rules[BGM_MAX_RULES];
} BGM_CONFIG;
_Static_assert(sizeof(BGM_RULE)==384,"rule ABI");
_Static_assert(offsetof(BGM_CONFIG,message)==80,"message ABI");
_Static_assert(offsetof(BGM_CONFIG,aliases)==512,"header ABI");
_Static_assert(offsetof(BGM_CONFIG,rules)==2688,"rule offset ABI");
_Static_assert(sizeof(BGM_CONFIG)==14976,"mapping ABI");
#endif
