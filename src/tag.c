/**
 * Tag parsing utilities
 *
 * These are separated from extension_util.c because they have no
 * library dependencies beyond the C standard library, making them
 * directly compilable into test binaries without dragging in the
 * full extension/value subsystem.
 */
#include <stdlib.h>
#include <string.h>

#include "compat/posix.h"

#include "asdf/version.h"
#include "tag.h"
#include "util.h"


asdf_tag_t *asdf_tag_parse(const char *tag) {
    if (!tag)
        return NULL;

    asdf_tag_t *res = calloc(1, sizeof(asdf_tag_t));

    if (!res)
        return NULL;

    // We assume that the version part of the tag comes after the first -
    // This is the convention that's always been used even though it's not
    // formally specified in the standard:
    // https://github.com/asdf-format/asdf-standard/issues/495
    const char *sep = strchr(tag, '-');

    if (!sep) {
        res->name = strdup(tag);
        if (!res->name) {
            free(res);
            return NULL;
        }
        res->version = NULL;
        return res;
    }

    size_t name_len = sep - tag;
    res->name = strndup(tag, name_len);

    if (!res->name) {
        free(res);
        return NULL;
    }

    char *version = strdup(sep + 1);
    res->version = asdf_version_parse(version);
    free(version);

    if (!res->version) {
        free((char *)res->name);
        free(res);
        return NULL;
    }

    return res;
}


void asdf_tag_destroy(asdf_tag_t *tag) {
    if (!tag)
        return;

    free((char *)tag->name);
    asdf_version_destroy((asdf_version_t *)tag->version);
    ZERO_MEMORY(tag, sizeof(asdf_tag_t));
    free(tag);
}
