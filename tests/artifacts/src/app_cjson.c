/*
 * app_cjson.c - Real Open-Source Program Driver #2: DaveGamble/cJSON v1.7.18
 * Parses, queries, mutates, constructs, and serializes JSON using libSystem-Lite.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static uint64_t fnv1a_str(const char* s) {
    uint64_t h = 14695981039346656037ULL;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

int main(void) {
    const char* input =
        "{\"device\":\"iPhone15,4\",\"soc\":\"T8120\","
        "\"cores\":6,\"ok\":true,\"note\":null,"
        "\"granule\":16384,\"pi\":3.14159,"
        "\"stages\":[191,192,193,{\"tag\":\"v3\",\"active\":false}]}";

    cJSON* root = cJSON_Parse(input);
    if (!root) {
        puts("CJSON_PARSE_FAIL");
        return 1;
    }

    const cJSON* dev = cJSON_GetObjectItemCaseSensitive(root, "device");
    const cJSON* gran = cJSON_GetObjectItemCaseSensitive(root, "granule");
    const cJSON* stages = cJSON_GetObjectItemCaseSensitive(root, "stages");
    const cJSON* pi = cJSON_GetObjectItemCaseSensitive(root, "pi");
    if (!cJSON_IsString(dev) || !cJSON_IsNumber(gran) || !cJSON_IsArray(stages) || !cJSON_IsNumber(pi)) {
        puts("CJSON_TYPE_FAIL");
        cJSON_Delete(root);
        return 2;
    }

    int arr_len = cJSON_GetArraySize(stages);
    const cJSON* nested = cJSON_GetArrayItem(stages, 3);
    const cJSON* tag = cJSON_GetObjectItemCaseSensitive(nested, "tag");

    /* Construct a mutated/derived tree and serialize round-trip */
    cJSON* out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "dev", dev->valuestring);
    cJSON_AddNumberToObject(out, "gran", gran->valueint + arr_len);
    cJSON_AddNumberToObject(out, "pi_x2", pi->valuedouble * 2.0);
    cJSON_AddStringToObject(out, "tag", cJSON_IsString(tag) ? tag->valuestring : "none");
    cJSON* nums = cJSON_CreateArray();
    for (int k = 0; k < 5; ++k) {
        cJSON_AddItemToArray(nums, cJSON_CreateNumber(k * 11 + 3));
    }
    cJSON_AddItemToObject(out, "nums", nums);

    char* printed = cJSON_PrintUnformatted(out);
    if (!printed) {
        puts("CJSON_PRINT_FAIL");
        cJSON_Delete(out);
        cJSON_Delete(root);
        return 3;
    }

    cJSON* reparsed = cJSON_Parse(printed);
    int equal = reparsed ? cJSON_Compare(out, reparsed, 1) : 0;
    uint64_t digest = fnv1a_str(printed);
    printf("CJSON_CMP=%d PRINTED=%s HASH=0x%016llx\n",
           equal, printed, (unsigned long long)digest);

    cJSON_free(printed);
    cJSON_Delete(reparsed);
    cJSON_Delete(out);
    cJSON_Delete(root);
    return equal ? 0 : 4;
}
