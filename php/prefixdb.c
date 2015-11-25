// This file is part of the PrefixDB library
// Copyright (c) 2014 Pierre-Yves Kerembellec <py.kerembellec@gmail.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "php.h"

#ifdef __cplusplus
extern "C"
{
#endif
#include <libprefixdb.h>
#include "khash.h"
#ifdef __cplusplus
}
#endif

typedef struct
{
    PREFIXDB *db;
    time_t   checked, modified;
} PREFIXDB_CACHE;
KHASH_MAP_INIT_STR(PREFIXDB_HASH, PREFIXDB_CACHE *)

typedef struct
{
    zend_object zo;
    PREFIXDB    *db;
    int         cached;
} PREFIXDB_OBJECT;

static zend_object_handlers prefixdb_object_handlers;

static inline PREFIXDB_OBJECT *php_prefixdb_fetch_object(zend_object *obj) {
    return (PREFIXDB_OBJECT *)((char *)obj - XtOffsetOf(PREFIXDB_OBJECT, zo));
}
#define Z_PREFIXDB_OBJ_P(zv) php_prefixdb_fetch_object(Z_OBJ_P(zv));

khash_t(PREFIXDB_HASH) *prefixdb_cache;
zend_class_entry *prefixdb_class_entry;

PHP_METHOD(PrefixDB, __construct)
{
    PREFIXDB_OBJECT *instance = Z_PREFIXDB_OBJ_P(getThis());
    PREFIXDB_CACHE  *cache;
    PREFIXDB        *pfdb;
    khiter_t        khit;
    struct stat     info;
    time_t          now;
    zend_string     *path;
    int             status;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|S", &path) == SUCCESS && (path && path->len))
    {
        if ((khit = kh_get(PREFIXDB_HASH, prefixdb_cache, path->val)) != kh_end(prefixdb_cache))
        {
            cache = kh_value(prefixdb_cache, khit);
            time(&now);
            if (now - cache->checked >= 30)
            {
                cache->checked = now;
                if (!stat(path->val, &info) && info.st_mtime > cache->modified && info.st_mtime <= (now - 5))
                {
                    if ((pfdb = prefixdb_load_file(path->val, 0)))
                    {
                        prefixdb_free(&(cache->db));
                        cache->db       = pfdb;
                        cache->modified = info.st_mtime;
                    }
                    else
                    {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING, "cannot open or invalid PrefixDB database \"%s\"", path->val);
                    }
                }
            }
            instance->db     = cache->db;
            instance->cached = 1;
        }
        else
        {
            if (!(instance->db = prefixdb_load_file(path->val, 0)))
            {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, "cannot open or invalid PrefixDB database \"%s\"", path->val);
            }
            else if ((cache = (PREFIXDB_CACHE *)calloc(1, sizeof(PREFIXDB_CACHE))))
            {
                time(&cache->checked);
                cache->modified  = cache->checked;
                cache->db        = instance->db;
                instance->cached = 1;
                if (!stat(path->val, &info))
                {
                    cache->modified = info.st_mtime;
                }
                khit = kh_put(PREFIXDB_HASH, prefixdb_cache, strdup(path->val), &status);
                kh_value(prefixdb_cache, khit) = cache;
            }
        }
    }
    else if (!instance->db)
    {
        instance->db = prefixdb_allocate();
    }
}

PHP_METHOD(PrefixDB, add)
{
    PREFIXDB_OBJECT *instance = Z_PREFIXDB_OBJ_P(getThis());
    // PREFIXDB_OBJECT *instance = (PREFIXDB_OBJECT *)zend_object_store_get_object(getThis() TSRMLS_CC);
    zend_string     *prefix;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &prefix) == SUCCESS && prefix->len && !instance->cached)
    {
        RETURN_LONG(prefixdb_add_string(instance->db, prefix->val, NULL));
    }
    RETURN_LONG(PREFIXDB_ERROR_PARAM);
}

PHP_METHOD(PrefixDB, save)
{
    PREFIXDB_OBJECT *instance = Z_PREFIXDB_OBJ_P(getThis());
    // PREFIXDB_OBJECT *instance = (PREFIXDB_OBJECT *)zend_object_store_get_object(getThis() TSRMLS_CC);
    zend_string     *path;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &path) == SUCCESS && path->len)
    {
        RETURN_LONG(prefixdb_save_file(instance->db, path->val));
    }
    RETURN_LONG(PREFIXDB_ERROR_PARAM);
}

PHP_METHOD(PrefixDB, search)
{
    PREFIXDB_OBJECT *instance = Z_PREFIXDB_OBJ_P(getThis());
    // PREFIXDB_OBJECT *instance = (PREFIXDB_OBJECT *)zend_object_store_get_object(getThis() TSRMLS_CC);
    zend_string     *address;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &address) == SUCCESS && address->len)
    {
        if (prefixdb_search_string(instance->db, address->val, NULL) == PREFIXDB_ERROR_OK)
        {
            RETURN_STRING("{}");
        }
    }
    RETURN_NULL();
}

static void prefixdb_dtor(zend_object *obj)
{
    PREFIXDB_OBJECT *i_obj = (PREFIXDB_OBJECT *)((char *)obj - XtOffsetOf(PREFIXDB_OBJECT, zo));
    zend_object_std_dtor(&i_obj->zo);
    if (!i_obj->cached)
    {
        prefixdb_free(&(i_obj->db));
    }
    i_obj = NULL;
    efree(i_obj);
}

static zend_object* prefixdb_ctor(zend_class_entry *ce TSRMLS_DC)
{
    PREFIXDB_OBJECT *instance;

    instance = ecalloc(1, sizeof(PREFIXDB_OBJECT));
    zend_object_std_init(&instance->zo, ce);
    instance->zo.handlers = zend_get_std_object_handlers();

    return &instance->zo;
}

const zend_function_entry prefixdb_functions[] =
{
    ZEND_FE_END
};

static zend_function_entry prefixdb_class_methods[] =
{
    PHP_ME(PrefixDB, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(PrefixDB, add,         NULL, ZEND_ACC_PUBLIC)
    PHP_ME(PrefixDB, save,        NULL, ZEND_ACC_PUBLIC)
    PHP_ME(PrefixDB, search,      NULL, ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};

PHP_MINIT_FUNCTION(prefixdb)
{
    zend_class_entry ce;

    memcpy(&prefixdb_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    prefixdb_object_handlers.offset = XtOffsetOf(PREFIXDB_OBJECT, zo);
    prefixdb_object_handlers.clone_obj = NULL;
    prefixdb_object_handlers.free_obj = prefixdb_dtor;

    INIT_CLASS_ENTRY(ce, "PrefixDB", prefixdb_class_methods);
    prefixdb_class_entry = zend_register_internal_class(&ce);
    prefixdb_class_entry->create_object = prefixdb_ctor;
    prefixdb_cache = kh_init(PREFIXDB_HASH);
    REGISTER_LONG_CONSTANT("PREFIXDB_ERROR_OK",       PREFIXDB_ERROR_OK,       CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_ERROR_PARAM",    PREFIXDB_ERROR_PARAM,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_ERROR_MEMORY",   PREFIXDB_ERROR_MEMORY,   CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_ERROR_ACCESS",   PREFIXDB_ERROR_ACCESS,   CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_ERROR_NOTFOUND", PREFIXDB_ERROR_NOTFOUND, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_FLAGS_COPY",     PREFIXDB_FLAGS_COPY,     CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("PREFIXDB_FLAGS_MMAP",     PREFIXDB_FLAGS_MMAP,     CONST_CS | CONST_PERSISTENT);
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(prefixdb)
{
    khiter_t khit;

    for (khit = kh_begin(prefixdb_cache); khit != kh_end(prefixdb_cache); ++khit)
    {
        if (kh_exist(prefixdb_cache, khit))
        {
            free((char *)kh_key(prefixdb_cache, khit));
            prefixdb_free(&(kh_value(prefixdb_cache, khit)->db));
            free(kh_value(prefixdb_cache, khit));
        }
    }
    kh_destroy(PREFIXDB_HASH, prefixdb_cache);
    return SUCCESS;
}

zend_module_entry prefixdb_module_entry =
{
    STANDARD_MODULE_HEADER,
    "prefixdb",
    prefixdb_functions,
    PHP_MINIT(prefixdb),
    PHP_MSHUTDOWN(prefixdb),
    NULL,
    NULL,
    NULL,
    "1.1",
    STANDARD_MODULE_PROPERTIES
};

#if defined(COMPILE_DL_PREFIXDB) || defined(HHVM)
ZEND_GET_MODULE(prefixdb)
#endif
