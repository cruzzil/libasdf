#include <stdatomic.h>

#include "types/asdf_common_tag_map.h"
#include "util.h"
#include "value_util.h"


static asdf_common_tag_map_t tag_map = {0};
static atomic_bool tag_map_initialized = false;


asdf_yaml_common_tag_t asdf_common_tag_get(const char *tagstr) {
    const asdf_common_tag_map_value *tag = asdf_common_tag_map_get(&tag_map, tagstr);

    if (!tag)
        return ASDF_YAML_COMMON_TAG_UNKNOWN;

    return tag->second;
}


ASDF_CONSTRUCTOR(asdf_common_tag_map_create) {
    if (atomic_load_explicit(&tag_map_initialized, memory_order_acquire))
        return;

    tag_map = c_make(
        asdf_common_tag_map,
        {{"tag:yaml.org,2002:null", ASDF_YAML_COMMON_TAG_NULL},
         {"tag:yaml.org,2002:bool", ASDF_YAML_COMMON_TAG_BOOL},
         {"tag:yaml.org,2002:int", ASDF_YAML_COMMON_TAG_INT},
         {"tag:yaml.org,2002:float", ASDF_YAML_COMMON_TAG_FLOAT},
         {"tag:yaml.org,2002:str", ASDF_YAML_COMMON_TAG_STR}});
    atomic_store_explicit(&tag_map_initialized, true, memory_order_release);
}


ASDF_DESTRUCTOR(asdf_common_tag_map_destroy) {
    if (atomic_load_explicit(&tag_map_initialized, memory_order_acquire)) {
        asdf_common_tag_map_drop(&tag_map);
        atomic_store_explicit(&tag_map_initialized, false, memory_order_release);
    }
}
