#include "otel_structs_common.h"

typedef struct Resource {
    KeyValue_t *attributes;
    uint32_t dropped_attributes_count;
    EntityRef_t *entity_refs;
} Resource_t;