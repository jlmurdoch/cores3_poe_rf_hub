#include <stdint.h>

typedef struct KeyValue KeyValue_t;
typedef union AnyValue AnyValue_t;

typedef struct KeyValueList {
    KeyValue_t *values;
} KeyValueList_t;

typedef struct ArrayValue {
    AnyValue_t *values;
} ArrayValue_t;

union AnyValue {
    char *string_value;
    bool bool_value;
    int64_t int_value;
    double double_value;

    ArrayValue_t array_value;
    KeyValueList_t kvlist_value;
    uint8_t *bytes_value;
};

struct KeyValue {
    char *key;
    bool has_value;
    uint8_t which_value;
    AnyValue_t value;
    struct KeyValue *next;
}; 

typedef struct EntityRef {
    char *schema_url;
    char *type;

    char **id_keys;
    char **description_keys;
} EntityRef_t;