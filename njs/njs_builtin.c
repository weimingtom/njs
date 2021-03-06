
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_boolean.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <njs_regexp.h>
#include <njs_date.h>
#include <njs_math.h>
#include <string.h>
#include <stdio.h>


typedef struct {
    njs_function_native_t  native;
    uint8_t                args_types[NJS_ARGS_TYPES_MAX];
} njs_function_init_t;


static nxt_int_t njs_builtin_completions(njs_vm_t *vm, size_t *size,
    const char **completions);


static const njs_object_init_t    *object_init[] = {
    NULL,                         /* global this        */
    &njs_math_object_init,        /* Math               */
};


static const njs_object_init_t  *prototype_init[] = {
    &njs_object_prototype_init,
    &njs_array_prototype_init,
    &njs_boolean_prototype_init,
    &njs_number_prototype_init,
    &njs_string_prototype_init,
    &njs_function_prototype_init,
    &njs_regexp_prototype_init,
    &njs_date_prototype_init,
};


static const njs_object_init_t    *constructor_init[] = {
    &njs_object_constructor_init,
    &njs_array_constructor_init,
    &njs_boolean_constructor_init,
    &njs_number_constructor_init,
    &njs_string_constructor_init,
    &njs_function_constructor_init,
    &njs_regexp_constructor_init,
    &njs_date_constructor_init,
};


static njs_ret_t
njs_prototype_function(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = njs_value_void;

    return NXT_OK;
}


nxt_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    nxt_int_t               ret;
    nxt_uint_t              i;
    njs_object_t            *objects;
    njs_function_t          *functions, *constructors;
    njs_object_prototype_t  *prototypes;

    static const njs_object_prototype_t  prototype_values[] = {
        /*
         * GCC 4 complains about uninitialized .shared field,
         * if the .type field is initialized as .object.type.
         */
        { .object =       { .type = NJS_OBJECT } },
        { .object =       { .type = NJS_ARRAY } },

        /*
         * The .object.type field must be initialzed after the .value field,
         * otherwise SunC 5.9 treats the .value as .object.value or so.
         */
        { .object_value = { .value = njs_value(NJS_BOOLEAN, 0, 0.0),
                            .object = { .type = NJS_OBJECT_BOOLEAN } } },

        { .object_value = { .value = njs_value(NJS_NUMBER, 0, 0.0),
                            .object = { .type = NJS_OBJECT_NUMBER } } },

        { .object_value = { .value = njs_string(""),
                            .object = { .type = NJS_OBJECT_STRING } } },

        { .function =     { .native = 1,
                            .args_offset = 1,
                            .u.native = njs_prototype_function,
                            .object = { .type = NJS_FUNCTION } } },

        { .object =       { .type = NJS_REGEXP } },

        { .date =         { .time = NAN,
                            .object = { .type = NJS_DATE } } },
    };

    static const njs_function_init_t  native_constructors[] = {
        /* SunC does not allow empty array initialization. */
        { njs_object_constructor,     { 0 } },
        { njs_array_constructor,      { 0 } },
        { njs_boolean_constructor,    { 0 } },
        { njs_number_constructor,     { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
        { njs_string_constructor,     { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_function_constructor,   { 0 } },
        { njs_regexp_constructor,
          { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_STRING_ARG } },
        { njs_date_constructor,       { 0 } },
    };

    static const njs_object_init_t    *function_init[] = {
        &njs_eval_function_init,      /* eval               */
        NULL,                         /* toString           */
        NULL,                         /* isNaN              */
        NULL,                         /* isFinite           */
        NULL,                         /* parseInt           */
        NULL,                         /* parseFloat         */
        NULL,                         /* encodeURI          */
        NULL,                         /* encodeURIComponent */
        NULL,                         /* decodeURI          */
        NULL,                         /* decodeURIComponent */
    };

    static const njs_function_init_t  native_functions[] = {
        /* SunC does not allow empty array initialization. */
        { njs_eval_function,               { 0 } },
        { njs_object_prototype_to_string,  { 0 } },
        { njs_number_global_is_nan,        { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
        { njs_number_is_finite,            { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
        { njs_number_parse_int,
          { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG } },
        { njs_number_parse_float,          { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_string_encode_uri,           { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_string_encode_uri_component, { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_string_decode_uri,           { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_string_decode_uri_component, { NJS_SKIP_ARG, NJS_STRING_ARG } },
    };

    static const njs_object_prop_t    null_proto_property = {
        .type = NJS_WHITEOUT,
        .name = njs_string("__proto__"),
        .value = njs_value(NJS_NULL, 0, 0.0),
    };

    static const njs_object_prop_t    function_prototype_property = {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_function_prototype_create),
    };

    ret = njs_object_hash_create(vm, &vm->shared->null_proto_hash,
                                 &null_proto_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    ret = njs_object_hash_create(vm, &vm->shared->function_prototype_hash,
                                 &function_prototype_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (object_init[i] != NULL) {
            ret = njs_object_hash_create(vm, &objects[i].shared_hash,
                                         object_init[i]->properties,
                                         object_init[i]->items);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }

        objects[i].shared = 1;
    }

    functions = vm->shared->functions;

    for (i = NJS_FUNCTION_EVAL; i < NJS_FUNCTION_MAX; i++) {
        if (function_init[i] != NULL) {
            ret = njs_object_hash_create(vm, &functions[i].object.shared_hash,
                                         function_init[i]->properties,
                                         function_init[i]->items);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }

        functions[i].object.shared = 1;
        functions[i].object.extensible = 1;
        functions[i].native = 1;
        functions[i].args_offset = 1;
        functions[i].u.native = native_functions[i].native;
        functions[i].args_types[0] = native_functions[i].args_types[0];
        functions[i].args_types[1] = native_functions[i].args_types[1];
        functions[i].args_types[2] = native_functions[i].args_types[2];
        functions[i].args_types[3] = native_functions[i].args_types[3];
        functions[i].args_types[4] = native_functions[i].args_types[4];
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        prototypes[i] = prototype_values[i];

        ret = njs_object_hash_create(vm, &prototypes[i].object.shared_hash,
                                     prototype_init[i]->properties,
                                     prototype_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    prototypes[NJS_PROTOTYPE_REGEXP].regexp.pattern =
                                              vm->shared->empty_regexp_pattern;

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        constructors[i].object.shared = 0;
        constructors[i].object.extensible = 1;
        constructors[i].native = 1;
        constructors[i].ctor = 1;
        constructors[i].args_offset = 1;
        constructors[i].u.native = native_constructors[i].native;
        constructors[i].args_types[0] = native_constructors[i].args_types[0];
        constructors[i].args_types[1] = native_constructors[i].args_types[1];
        constructors[i].args_types[2] = native_constructors[i].args_types[2];
        constructors[i].args_types[3] = native_constructors[i].args_types[3];
        constructors[i].args_types[4] = native_constructors[i].args_types[4];

        ret = njs_object_hash_create(vm, &constructors[i].object.shared_hash,
                                     constructor_init[i]->properties,
                                     constructor_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    return NXT_OK;
}


/*
 * Object(),
 * Object.__proto__             -> Function_Prototype,
 * Object_Prototype.__proto__   -> null,
 *   the null value is handled by njs_object_prototype_get_proto(),
 *
 * Array(),
 * Array.__proto__              -> Function_Prototype,
 * Array_Prototype.__proto__    -> Object_Prototype,
 *
 * Boolean(),
 * Boolean.__proto__            -> Function_Prototype,
 * Boolean_Prototype.__proto__  -> Object_Prototype,
 *
 * Number(),
 * Number.__proto__             -> Function_Prototype,
 * Number_Prototype.__proto__   -> Object_Prototype,
 *
 * String(),
 * String.__proto__             -> Function_Prototype,
 * String_Prototype.__proto__   -> Object_Prototype,
 *
 * Function(),
 * Function.__proto__           -> Function_Prototype,
 * Function_Prototype.__proto__ -> Object_Prototype,
 *
 * RegExp(),
 * RegExp.__proto__             -> Function_Prototype,
 * RegExp_Prototype.__proto__   -> Object_Prototype,
 *
 * Date(),
 * Date.__proto__               -> Function_Prototype,
 * Date_Prototype.__proto__     -> Object_Prototype,
 *
 * eval(),
 * eval.__proto__               -> Function_Prototype.
 */

nxt_int_t
njs_builtin_objects_clone(njs_vm_t *vm)
{
    size_t        size;
    nxt_uint_t    i;
    njs_value_t   *values;
    njs_object_t  *object_prototype, *function_prototype;

    /*
     * Copy both prototypes and constructors arrays by one memcpy()
     * because they are stored together.
     */
    size = NJS_PROTOTYPE_MAX * sizeof(njs_object_prototype_t)
           + NJS_CONSTRUCTOR_MAX * sizeof(njs_function_t);

    memcpy(vm->prototypes, vm->shared->prototypes, size);

    object_prototype = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;

    for (i = NJS_PROTOTYPE_ARRAY; i < NJS_PROTOTYPE_MAX; i++) {
        vm->prototypes[i].object.__proto__ = object_prototype;
    }

    function_prototype = &vm->prototypes[NJS_CONSTRUCTOR_FUNCTION].object;
    values = vm->scopes[NJS_SCOPE_GLOBAL];

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        values[i].type = NJS_FUNCTION;
        values[i].data.truth = 1;
        values[i].data.u.function = &vm->constructors[i];
        vm->constructors[i].object.__proto__ = function_prototype;
    }

    return NXT_OK;
}


const char **
njs_vm_completions(njs_vm_t *vm)
{
    size_t      size;
    const char  **completions;

    if (njs_builtin_completions(vm, &size, NULL) != NXT_OK) {
        return NULL;
    }

    completions = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                       sizeof(char *) * (size + 1));

    if (completions == NULL) {
        return NULL;
    }

    if (njs_builtin_completions(vm, NULL, completions) != NXT_OK) {
        return NULL;
    }

    return completions;
}


static nxt_int_t
njs_builtin_completions(njs_vm_t *vm, size_t *size, const char **completions)
{
    char                    *compl;
    size_t                  n, len;
    nxt_str_t               string;
    nxt_uint_t              i, k;
    njs_object_t            *objects;
    njs_keyword_t           *keyword;
    njs_function_t          *constructors;
    njs_object_prop_t       *prop;
    nxt_lvlhsh_each_t       lhe;
    njs_object_prototype_t  *prototypes;

    n = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_keyword_hash_proto);

    for ( ;; ) {
        keyword = nxt_lvlhsh_each(&vm->shared->keywords_hash, &lhe);

        if (keyword == NULL) {
            break;
        }

        if (completions != NULL) {
            completions[n++] = (char *) keyword->name.start;

        } else {
            n++;
        }
    }

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (object_init[i] == NULL) {
            continue;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&objects[i].shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = object_init[i]->name.length + string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, "%s.%s", object_init[i]->name.start,
                         string.start);

                completions[n++] = (char *) compl;

            } else {
                n++;
            }
        }
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&prototypes[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, ".%s", string.start);

                for (k = 0; k < n; k++) {
                    if (strncmp(completions[k], compl, len) == 0) {
                        break;
                    }
                }

                if (k == n) {
                    completions[n++] = (char *) compl;
                }

            } else {
                n++;
            }
        }
    }

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&constructors[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = constructor_init[i]->name.length + string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, "%s.%s", constructor_init[i]->name.start,
                         string.start);

                completions[n++] = (char *) compl;

            } else {
                n++;
            }
        }
    }

    if (size) {
        *size = n;
    }

    return NXT_OK;
}


nxt_int_t
njs_builtin_match_native_function(njs_vm_t *vm, njs_function_t *function,
    nxt_str_t *name)
{
    char                    *buf;
    size_t                  len;
    nxt_str_t               string;
    nxt_uint_t              i;
    njs_object_t            *objects;
    njs_function_t          *constructors;
    njs_object_prop_t       *prop;
    nxt_lvlhsh_each_t       lhe;
    njs_object_prototype_t  *prototypes;

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (object_init[i] == NULL) {
            continue;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&objects[i].shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = object_init[i]->name.length + string.length
                      + sizeof(".");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.%s", object_init[i]->name.start,
                         string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&prototypes[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = prototype_init[i]->name.length + string.length
                      + sizeof(".prototype.");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.prototype.%s",
                         prototype_init[i]->name.start, string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&constructors[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = constructor_init[i]->name.length + string.length
                      + sizeof(".");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.%s", constructor_init[i]->name.start,
                         string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    return NXT_DECLINED;
}
