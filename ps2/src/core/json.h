#ifndef PS2_JSON_H
#define PS2_JSON_H

#include "ps2_types.h"

#include <stddef.h>

/* A small JSON reader for save files and the extracted data tables.
 * Parses into a tree; nothing here is fast and nothing needs to be. */

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType   type;
    double     number;
    bool       boolean;
    char      *string;      /* JSON_STRING, and the key when in an object */
    char      *key;
    JsonValue *child;       /* first element / member */
    JsonValue *next;        /* sibling */
};

/* NULL on a syntax error; *error_offset says where. */
JsonValue *json_parse(const char *text, size_t *error_offset);
JsonValue *json_parse_file(const char *path);
void       json_free(JsonValue *v);

/* Lookups return NULL / the fallback when the member is absent or of the
 * wrong type, so a hand-edited file with a field missing still loads. */
const JsonValue *json_get(const JsonValue *obj, const char *key);
const JsonValue *json_at(const JsonValue *arr, int index);
int              json_length(const JsonValue *arr);
long             json_int(const JsonValue *obj, const char *key, long fallback);
bool             json_bool(const JsonValue *obj, const char *key, bool fallback);
const char      *json_str(const JsonValue *obj, const char *key,
                          const char *fallback);

#endif /* PS2_JSON_H */
