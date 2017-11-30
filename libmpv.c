/*
 * Copyright (c) 2017 Rodger Combs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <mpv/client.h>

#include <node_api.h>
#include <assert.h>
#include <locale.h>
#include <stdlib.h>

#include <uv.h>

#define countof(x) (sizeof(x) / sizeof(x[0]))
#define ASSERT_OK(x) assert((x) == napi_ok)

#define RETURN_UNDEFINED() \
{ \
  napi_value undefined_val; \
  ASSERT_OK(napi_get_undefined(env, &undefined_val)); \
  return undefined_val; \
}

#define THROW_ANY_ERROR(fn, code, msg, retcode) \
{ \
  ASSERT_OK(fn(env, code, msg)); \
  retcode \
}

#define THROW_ERRORF(code, msg, retcode) THROW_ANY_ERROR(napi_throw_error, (code), (msg), retcode)
#define THROW_TYPE_ERRORF(code, msg, retcode) THROW_ANY_ERROR(napi_throw_type_error, (code), (msg), retcode)
#define THROW_RANGE_ERRORF(code, msg, retcode) THROW_ANY_ERROR(napi_throw_range_error, (code), (msg), retcode)


#define THROW_ERROR(code, msg) THROW_ERRORF((code), (msg), RETURN_UNDEFINED())
#define THROW_TYPE_ERROR(code, msg) THROW_TYPE_ERRORF((code), (msg), RETURN_UNDEFINED())
#define THROW_RANGE_ERROR(code, msg) THROW_RANGE_ERRORF((code), (msg), RETURN_UNDEFINED())

#define THROW_ENOMEMF(retcode) THROW_ERRORF("MPV_ENOMEM", "Failed to allocate sufficient memory", retcode)
#define THROW_ENOMEM() THROW_ENOMEMF(RETURN_UNDEFINED())

#define RETURN_STRING(str) \
{ \
  const char *strval = (str); \
  if (!strval) \
    RETURN_UNDEFINED() \
  napi_value ret; \
  ASSERT_OK(napi_create_string_utf8(env, strval, NAPI_AUTO_LENGTH, &ret)); \
  return ret; \
}

#define RETURN_INT64(val) \
{ \
  int64_t intval = (val); \
  napi_value ret; \
  ASSERT_OK(napi_create_int64(env, intval, &ret)); \
  return ret; \
}

#define RETURN_INT_ERROR(x) \
{ \
  int retint = (x); \
  if (retint < 0) {\
    THROW_ERROR("MPV_ERROR_CODE", mpv_error_string(retint)) \
  } else {\
    napi_value retval; \
    ASSERT_OK(napi_create_int32(env, retint, &retval)); \
    return retval; \
  } \
}

#define GET_POINTER(buf, dstvar) \
{ \
  napi_value value = (buf); \
  bool is_buf; \
  ASSERT_OK(napi_is_buffer(env, value, &is_buf)); \
  if (!is_buf) \
    THROW_TYPE_ERROR("MPV_EXPECTED_POINTER", "Expected a pointer buffer") \
  void* buf_data; \
  size_t buf_length; \
  ASSERT_OK(napi_get_buffer_info(env, value, &buf_data, &buf_length)); \
  if (buf_length != sizeof(void*)) \
    THROW_TYPE_ERROR("MPV_EXPECTED_POINTER", "Expected a pointer buffer") \
  *(void**)dstvar = *(void**)buf_data; \
}

#define GET_STRING_SF(str, dstvar, size, failcode) \
{ \
  napi_value value = (str); \
  napi_status status = napi_get_value_string_utf8(env, value, NULL, 0, &size); \
  if (status == napi_string_expected) { \
    dstvar = NULL; \
  } else { \
    ASSERT_OK(status); \
    size++; \
    dstvar = malloc(size); \
    if (!dstvar) \
      THROW_ENOMEMF(failcode) \
    ASSERT_OK(napi_get_value_string_utf8(env, value, dstvar, size, &size)); \
  } \
}

#define GET_STRINGF(str, dstvar, failcode) \
{ \
  size_t size; \
  GET_STRING_SF((str), dstvar, size, failcode); \
}

#define GET_STRING(str, dstvar) GET_STRINGF(str, dstvar, RETURN_UNDEFINED())

#define GET_WRAP(jsthis, dstvar, typeref) \
{ \
  napi_value cons; \
  ASSERT_OK(napi_get_reference_value(env, typeref, &cons)); \
  bool isinstance; \
  ASSERT_OK(napi_instanceof(env, jsthis, cons, &isinstance)); \
  if (!isinstance) \
    THROW_TYPE_ERROR("MPV_BAD_THIS", "Called with invalid this pointer") \
  ASSERT_OK(napi_unwrap(env, jsthis, (void**)&dstvar)); \
}

#define MAKE_POINTER(ptr, dstvar) \
  ASSERT_OK(napi_create_buffer_copy(env, sizeof(void*), &ptr, NULL, &dstvar));

static napi_value GetAPIVersion(napi_env env, napi_callback_info info) {
  napi_value num;
  ASSERT_OK(napi_create_int64(env, mpv_client_api_version(), &num));

  return num;
}

static napi_value ErrorString(napi_env env, napi_callback_info info) {
  napi_status status;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, NULL, NULL));

  int32_t arg;
  status = napi_get_value_int32(env, args[0], &arg);
  if (status == napi_number_expected)
    THROW_TYPE_ERROR("MPV_EXPECTED_NUMBER", "Expected a number value")
  else
    ASSERT_OK(status);

  RETURN_STRING(mpv_error_string(arg))
}

static napi_value EventName(napi_env env, napi_callback_info info) {
  napi_status status;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, NULL, NULL));

  int32_t arg;
  status = napi_get_value_int32(env, args[0], &arg);
  if (status == napi_number_expected)
    THROW_TYPE_ERROR("MPV_EXPECTED_NUMBER", "Expected a number value")
  else
    ASSERT_OK(status);

  RETURN_STRING(mpv_event_name(arg))
}

static napi_value Free(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, NULL, NULL));

  void *data;
  GET_POINTER(args[0], &data)

  mpv_free(data);

  RETURN_UNDEFINED()
}

static napi_ref handle_constructor;

typedef struct libmpv_wrap {
  mpv_handle *handle;
  napi_ref ref;
  napi_env env;
  uv_async_t async;
} libmpv_wrap;

static void DestroyHandle(napi_env env, void* finalize_data, void* finalize_hint) {
  libmpv_wrap *wrap = finalize_data;
  mpv_terminate_destroy(wrap->handle);
  uv_close((uv_handle_t*)&wrap->async, NULL);
  napi_delete_reference(env, wrap->ref);
  free(wrap);
}

#define GET_HANDLE_WRAP1(jsthis, dstvar) GET_WRAP(jsthis, dstvar, handle_constructor)

#define GET_HANDLE_WRAP(jsthis, dstvar) \
{ \
  libmpv_wrap *wrap; \
  GET_HANDLE_WRAP1(jsthis, wrap) \
  dstvar = wrap->handle; \
}

static bool GetValueForNode(napi_env env, mpv_node *node, napi_value *val);

static bool GetValueForMpvFormat(napi_env env, mpv_format format, void *data, napi_value *val) {
  switch (format) {
  case MPV_FORMAT_STRING:
    ASSERT_OK(napi_create_string_utf8(env, *(char**)data, NAPI_AUTO_LENGTH, val)); //FIXME: what if it's invalid UTF-8?
    return true;
  case MPV_FORMAT_FLAG:
    ASSERT_OK(napi_get_boolean(env, *(int*)data, val));
    return true;
  case MPV_FORMAT_INT64:
    ASSERT_OK(napi_create_int64(env, *(int64_t*)data, val));
    return true;
  case MPV_FORMAT_DOUBLE:
    ASSERT_OK(napi_create_double(env, *(double*)data, val));
    return true;
  case MPV_FORMAT_NODE_ARRAY:
    {
      mpv_node_list* list = data;
      ASSERT_OK(napi_create_array_with_length(env, list->num, val));
      for (int i = 0; i < list->num; i++) {
        napi_value list_value;
        if (!GetValueForNode(env, &list->values[i], &list_value)) {
          return false;
        } else {
          ASSERT_OK(napi_set_element(env, *val, i, list_value));
        }
      }
    }
  case MPV_FORMAT_NODE_MAP:
    {
      ASSERT_OK(napi_create_object(env, val));
      mpv_node_list* list = data;
      for (int i = 0; i < list->num; i++) {
        napi_value list_value;
        if (!GetValueForNode(env, &list->values[i], &list_value)) {
          return false;
        } else {
          ASSERT_OK(napi_set_named_property(env, *val, list->keys[i], list_value));
        }
      }
    }
  case MPV_FORMAT_BYTE_ARRAY:
    {
      mpv_byte_array *ba = data;
      ASSERT_OK(napi_create_buffer_copy(env, ba->size, ba->data, NULL, val));
      return true;
    }
  case MPV_FORMAT_NONE:
    ASSERT_OK(napi_get_undefined(env, val));
    return true;
  default:
    THROW_TYPE_ERRORF("MPV_NODE_TYPE", "Invalid type for mpv_node", return false;)
  }
}

static bool GetValueForNode(napi_env env, mpv_node *node, napi_value *val) {
  return GetValueForMpvFormat(env, node->format, &node->u, val);
}

static napi_value GetValueForEvent(napi_env env, mpv_event *event) {
  napi_value retval;
  ASSERT_OK(napi_create_object(env, &retval));
  napi_value subval;
  ASSERT_OK(napi_create_int32(env, event->event_id, &subval));
  ASSERT_OK(napi_set_named_property(env, retval, "event_id", subval));
  ASSERT_OK(napi_create_int32(env, event->error, &subval));
  ASSERT_OK(napi_set_named_property(env, retval, "error", subval));
  ASSERT_OK(napi_create_int64(env, event->reply_userdata, &subval));
  ASSERT_OK(napi_set_named_property(env, retval, "reply_userdata", subval));
  switch (event->event_id) {
  case MPV_EVENT_GET_PROPERTY_REPLY:
  case MPV_EVENT_PROPERTY_CHANGE:
    {
      mpv_event_property *prop = event->data;
      ASSERT_OK(napi_create_string_utf8(env, prop->name, NAPI_AUTO_LENGTH, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "property_name", subval));
      if (!GetValueForMpvFormat(env, prop->format, prop->data, &subval))
        return false;
      ASSERT_OK(napi_set_named_property(env, retval, "property_value", subval));
    }
    break;
  case MPV_EVENT_LOG_MESSAGE:
    {
      mpv_event_log_message *msg = event->data;
      ASSERT_OK(napi_create_string_utf8(env, msg->prefix, NAPI_AUTO_LENGTH, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "log_prefix", subval));
      ASSERT_OK(napi_create_string_utf8(env, msg->text, NAPI_AUTO_LENGTH, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "log_text", subval));
      ASSERT_OK(napi_create_int32(env, msg->log_level, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "log_level", subval));
    }
    break;
  case MPV_EVENT_CLIENT_MESSAGE:
    {
      mpv_event_client_message *cmsg = event->data;
      ASSERT_OK(napi_create_array_with_length(env, cmsg->num_args, &subval));
      for (int i = 0; i < cmsg->num_args; i++) {
        napi_value argval;
        ASSERT_OK(napi_create_string_utf8(env, cmsg->args[i], NAPI_AUTO_LENGTH, &argval));
        ASSERT_OK(napi_set_element(env, subval, i, argval));
      }
      ASSERT_OK(napi_set_named_property(env, retval, "message_args", subval));
    }
    break;
  case MPV_EVENT_END_FILE:
    {
      mpv_event_end_file *ef = event->data;
      ASSERT_OK(napi_create_int32(env, ef->reason, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "eof_reason", subval));
      ASSERT_OK(napi_create_int32(env, ef->error, &subval));
      ASSERT_OK(napi_set_named_property(env, retval, "eof_error", subval));
    }
    break;
  default:
    break;
  }
  return retval;
}

static void HandleAsyncCallback(uv_async_t *async) {
  libmpv_wrap *wrap = async->data;
  napi_value jsthis;
  ASSERT_OK(napi_get_reference_value(wrap->env, wrap->ref, &jsthis));
  napi_value emit;
  ASSERT_OK(napi_get_named_property(wrap->env, jsthis, "emit", &emit));
  napi_value argv[2];
  ASSERT_OK(napi_create_string_utf8(wrap->env, "mpv_event", NAPI_AUTO_LENGTH, &argv[0]));
  if (!jsthis)
    return;
  mpv_event *event;
  while ((event = mpv_wait_event(wrap->handle, 0))) {
    if (event->event_id == MPV_EVENT_NONE)
      break;
    if ((argv[1] = GetValueForEvent(wrap->env, event)))
      ASSERT_OK(napi_make_callback(wrap->env, NULL, jsthis, emit, 2, argv, NULL));
  }
}

static void HandleWakeupCallback(void *data) {
  libmpv_wrap *wrap = data;
  uv_async_send(&wrap->async);
}

static napi_value ConstructHandle(napi_env env, napi_callback_info info) {
  napi_value target;
  ASSERT_OK(napi_get_new_target(env, info, &target));
  bool is_constructor = target != NULL;

  if (is_constructor) {
    // Invoked as constructor: `new libmpv.handle()`
    napi_value jsthis;
    ASSERT_OK(napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));

    libmpv_wrap *wrap = malloc(sizeof(libmpv_wrap));
    if (!wrap)
      THROW_ENOMEM()

    setlocale(LC_NUMERIC, "C");
    wrap->handle = mpv_create();
    if (!wrap->handle) {
      free(wrap);
      THROW_ERROR("MPV_CREATE_FAILED", "Failed to create mpv handle")
    }

    wrap->env = env;

    uv_async_init(uv_default_loop(), &wrap->async, HandleAsyncCallback);
    wrap->async.data = wrap;

    ASSERT_OK(napi_wrap(env, jsthis, wrap, DestroyHandle, NULL, &wrap->ref));

    mpv_set_wakeup_callback(wrap->handle, HandleWakeupCallback, wrap);

    napi_value setupEvents;
    ASSERT_OK(napi_get_named_property(env, jsthis, "setupEvents", &setupEvents));
    ASSERT_OK(napi_call_function(env, jsthis, setupEvents, 0, NULL, NULL));

    return jsthis;
  } else {
    // Invoked as plain function `libmpv.handle()`, turn into construct call.
    napi_value cons;
    ASSERT_OK(napi_get_reference_value(env, handle_constructor, &cons));

    napi_value instance;
    ASSERT_OK(napi_new_instance(env, cons, 0, NULL, &instance));

    return instance;
  }
}

static napi_value GetPointer(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  ASSERT_OK(napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  napi_value ret;
  MAKE_POINTER(handle, ret)

  return ret;
}

static napi_value GetHandleClientName(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  ASSERT_OK(napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  RETURN_STRING(mpv_client_name(handle))
}

static napi_value InitializeHandle(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  ASSERT_OK(napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  RETURN_INT_ERROR(mpv_initialize(handle))
}

/*
napi_value CreateClient(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));

  GET_HANDLE_WRAP(jsthis, handle);

  char *name = NULL;
  if (argc > 0) {
    GET_STRING(args[0], name)
    if (!name)
      THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")
  }

  RETURN_INT_ERROR(mpv_initialize(handle))
}
*/

static napi_value LoadConfigFile(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *name = NULL;
  GET_STRING(args[0], name)
  if (!name)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  int ret = mpv_load_config_file(handle, name);
  free(name);
  RETURN_INT_ERROR(ret);
}

static napi_value GetTimeUs(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  ASSERT_OK(napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  RETURN_INT64(mpv_get_time_us(handle))
}

typedef struct free_list {
  void **frees;
  size_t n_frees;
} free_list;

static void FreeList(free_list *list) {
  if (list->frees) {
    for (size_t i = 0; i < list->n_frees; i++)
      free(list->frees[i]);
    free(list->frees);
  }
}

static bool AddToFreeList(free_list *list, void *ptr) {
  size_t count = list->n_frees;
  if (!(count & (count - 1))) {
    if (count >= SIZE_MAX / sizeof(void*) / 2)
      return false;
    count = count ? count << 1 : 1;
    void *newarr = realloc(list->frees, count * sizeof(void*));
    if (!newarr)
      return false;
    list->frees = newarr;
  }
  list->frees[list->n_frees++] = ptr;
  return true;
}

static bool AllocateNodeList(napi_env env, napi_value val, mpv_node *dest, free_list *freelist) {
  uint32_t size;
  ASSERT_OK(napi_get_array_length(env, val, &size));
  if (size > INT32_MAX)
    THROW_RANGE_ERRORF("MPV_ARRAY_TOO_LARGE", "Array/object larger than INT32_MAX elements", return false;)
  mpv_node *children = calloc(sizeof(mpv_node), size);
  if (!children)
    THROW_ENOMEMF(return false;)
  if (!AddToFreeList(freelist, children)) {
    free(children);
    THROW_ENOMEMF(return false;)
  }
  mpv_node_list *nodelist = calloc(sizeof(mpv_node_list), 1);
  if (!nodelist)
    THROW_ENOMEMF(return false;)
  if (!AddToFreeList(freelist, nodelist)) {
    free(nodelist);
    THROW_ENOMEMF(return false;)
  }
  nodelist->num = size;
  nodelist->values = children;
  dest->u.list = nodelist;
  return true;
}

static bool GetNodeForValue(napi_env env, napi_value val, mpv_node *dest, free_list *freelist) {
  napi_valuetype type;
  napi_status status = napi_typeof(env, val, &type);
  if (status == napi_invalid_arg)
    return false;
  else
    ASSERT_OK(status);

  switch (type) {
  case napi_undefined:
  case napi_null:
    dest->format = MPV_FORMAT_NONE;
    return true;
  case napi_boolean:
    {
      dest->format = MPV_FORMAT_FLAG;
      bool boolval;
      ASSERT_OK(napi_get_value_bool(env, val, &boolval));
      dest->u.flag = boolval;
      return true;
    }
  case napi_number:
    dest->format = MPV_FORMAT_DOUBLE;
    ASSERT_OK(napi_get_value_double(env, val, &dest->u.double_));
    return true;
  case napi_string:
    dest->format = MPV_FORMAT_STRING;
    GET_STRINGF(val, dest->u.string, return false;);
    if (!AddToFreeList(freelist, dest->u.string)) {
      free(dest->u.string);
      THROW_ENOMEMF(return false;);
    }
    return true;
  case napi_object:
    {
      bool is_type;
      ASSERT_OK(napi_is_buffer(env, val, &is_type));
      if (is_type) {
        dest->format = MPV_FORMAT_BYTE_ARRAY;

        mpv_byte_array *byte_array = calloc(sizeof(mpv_byte_array), 1);
        if (!byte_array)
          THROW_ENOMEMF(return false;)
        if (!AddToFreeList(freelist, byte_array)) {
          free(byte_array);
          THROW_ENOMEMF(return false;)
        }

        dest->u.ba = byte_array;

        ASSERT_OK(napi_get_buffer_info(env, val, &dest->u.ba->data, &dest->u.ba->size));
        return true;
      }

      ASSERT_OK(napi_is_arraybuffer(env, val, &is_type));
      if (is_type) {
        dest->format = MPV_FORMAT_BYTE_ARRAY;

        mpv_byte_array *byte_array = calloc(sizeof(mpv_byte_array), 1);
        if (!byte_array)
          THROW_ENOMEMF(return false;)
        if (!AddToFreeList(freelist, byte_array)) {
          free(byte_array);
          THROW_ENOMEMF(return false;)
        }

        dest->u.ba = byte_array;

        ASSERT_OK(napi_get_arraybuffer_info(env, val, &dest->u.ba->data, &dest->u.ba->size));
        return true;
      }

      ASSERT_OK(napi_is_array(env, val, &is_type));
      if (is_type) {
        dest->format = MPV_FORMAT_NODE_ARRAY;
        if (!AllocateNodeList(env, val, dest, freelist))
          return false;
        for (int i = 0; i < dest->u.list->num; i++) {
          napi_value list_val;
          ASSERT_OK(napi_get_element(env, val, i, &list_val));
          if (!GetNodeForValue(env, list_val, &dest->u.list->values[i], freelist))
            return false;
        }
        return true;
      }

      dest->format = MPV_FORMAT_NODE_MAP;
      napi_value prop_names;
      ASSERT_OK(napi_get_property_names(env, val, &prop_names));
      if (!AllocateNodeList(env, prop_names, dest, freelist))
        return false;
      dest->u.list->keys = calloc(sizeof(char*), dest->u.list->num);
      if (!dest->u.list->keys || !AddToFreeList(freelist, dest->u.list->keys))
          THROW_ENOMEMF(return false;);
      for (int i = 0; i < dest->u.list->num; i++) {
        napi_value key_val;
        ASSERT_OK(napi_get_element(env, prop_names, i, &key_val));

        GET_STRINGF(key_val, dest->u.list->keys[i], return false;)
        if (!dest->u.list->keys[i])
          return false;

        if (!AddToFreeList(freelist, dest->u.list->keys[i]))
          THROW_ENOMEMF(return false;);

        napi_value val_val;
        ASSERT_OK(napi_get_property(env, val, key_val, &val_val));
        if (!GetNodeForValue(env, val_val, &dest->u.list->values[i], freelist))
          return false;
      }
      return true;
    }
  default:
    THROW_TYPE_ERRORF("MPV_NODE_TYPE", "Invalid type for mpv_node", return false;)
  }
}

static napi_value SetHandleProperty(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 2;
  napi_value args[2];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *name = NULL;
  GET_STRING(args[0], name)
  if (!name)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  mpv_node node = {{0}, 0};
  free_list list = {0, 0};
  if (!GetNodeForValue(env, args[1], &node, &list)) {
    free(name);
    FreeList(&list);
    RETURN_UNDEFINED()
  }

  int ret = mpv_set_property(handle, name, MPV_FORMAT_NODE, &node);

  free(name);
  FreeList(&list);
  RETURN_INT_ERROR(ret);
}

static napi_value SetHandlePropertyPointer(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 2;
  napi_value args[2];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *name = NULL;
  GET_STRING(args[0], name)
  if (!name)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  void* ptr;
  GET_POINTER(args[1], &ptr)

  int ret = mpv_set_property(handle, name, MPV_FORMAT_INT64, &ptr);

  free(name);
  RETURN_INT_ERROR(ret);
}

static napi_value GetHandleProperty(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *name = NULL;
  GET_STRING(args[0], name)
  if (!name)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  mpv_node node = {{0}, 0};

  int ret = mpv_get_property(handle, name, MPV_FORMAT_NODE, &node);
  free(name);
  if (ret < 0)
    THROW_ERROR("MPV_ERROR_CODE", mpv_error_string(ret))

  napi_value retval;
  ret = GetValueForNode(env, &node, &retval);
  mpv_free_node_contents(&node);

  if (!ret)
    RETURN_UNDEFINED()

  return retval;
}

static napi_value RunCommand(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  mpv_node node = {{0}, 0}, retnode = {{0}, 0};
  free_list list = {0, 0};
  if (!GetNodeForValue(env, args[0], &node, &list)) {
    FreeList(&list);
    RETURN_UNDEFINED()
  }

  int ret = mpv_command_node(handle, &node, &retnode);
  FreeList(&list);
  if (ret < 0)
    THROW_ERROR("MPV_ERROR_CODE", mpv_error_string(ret))

  napi_value retval;
  ret = GetValueForNode(env, &retnode, &retval);
  mpv_free_node_contents(&retnode);

  if (!ret)
    RETURN_UNDEFINED()

  return retval;
}


static napi_value RunCommandString(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *command = NULL;
  GET_STRING(args[0], command)
  if (!command)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  int ret = mpv_command_string(handle, command);
  free(command);
  RETURN_INT_ERROR(ret);
}

static napi_value RequestEvent(napi_env env, napi_callback_info info) {
  napi_status status;
  napi_value jsthis;
  size_t argc = 2;
  napi_value args[2];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  int32_t event;
  status = napi_get_value_int32(env, args[0], &event);
  if (status == napi_number_expected)
    THROW_TYPE_ERROR("MPV_EXPECTED_NUMBER", "Expected a number value")
  else
    ASSERT_OK(status);

  bool enable;
  status = napi_get_value_bool(env, args[1], &enable);
  if (status == napi_boolean_expected)
    THROW_TYPE_ERROR("MPV_EXPECTED_NUMBER", "Expected a boolean value")
  else
    ASSERT_OK(status);

  RETURN_INT_ERROR(mpv_request_event(handle, event, enable));
}

static napi_value RequestLogMessages(napi_env env, napi_callback_info info) {
  napi_value jsthis;
  size_t argc = 1;
  napi_value args[1];
  ASSERT_OK(napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));
  mpv_handle* handle;
  GET_HANDLE_WRAP(jsthis, handle);

  char *level = NULL;
  GET_STRING(args[0], level)
  if (!level)
    THROW_TYPE_ERROR("MPV_EXPECTED_STRING", "Expected a string value")

  int ret = mpv_request_log_messages(handle, level);
  free(level);
  RETURN_INT_ERROR(ret);
}

#define DECLARE_NAPI_METHOD(name, func)                          \
  { name, 0, func, 0, 0, 0, napi_default, 0 }

static napi_value Init(napi_env env, napi_value exports) {
  napi_value handle;
  napi_property_descriptor handle_properties[] = {
    { "pointer", 0, 0, GetPointer, 0, 0, napi_default, 0 },
    { "client_name", 0, 0, GetHandleClientName, 0, 0, napi_default, 0 },
    DECLARE_NAPI_METHOD("initialize", InitializeHandle),
    //DECLARE_NAPI_METHOD("create_client", CreateClient),
    DECLARE_NAPI_METHOD("load_config_file", LoadConfigFile),
    { "time_us", 0, 0, GetTimeUs, 0, 0, napi_default, 0 },
    DECLARE_NAPI_METHOD("set_property", SetHandleProperty),
    DECLARE_NAPI_METHOD("set_property_pointer", SetHandlePropertyPointer),
    DECLARE_NAPI_METHOD("get_property", GetHandleProperty),
    DECLARE_NAPI_METHOD("command", RunCommand),
    DECLARE_NAPI_METHOD("command_string", RunCommandString),
    DECLARE_NAPI_METHOD("request_event", RequestEvent),
    DECLARE_NAPI_METHOD("request_log_messages", RequestLogMessages),
  };
  ASSERT_OK(napi_define_class(env, "mpv_handle", NAPI_AUTO_LENGTH,
                              ConstructHandle, NULL,
                              countof(handle_properties), handle_properties,
                              &handle));
  ASSERT_OK(napi_create_reference(env, handle, 1, &handle_constructor));

  napi_property_descriptor export_properties[] = {
    { "api_version", 0, 0, GetAPIVersion, 0, 0, napi_default, 0 },
    DECLARE_NAPI_METHOD("error_string", ErrorString),
    DECLARE_NAPI_METHOD("free", Free),
    { "handle", 0, 0, 0, 0, handle, napi_default, 0 },
    DECLARE_NAPI_METHOD("event_name", EventName),
  };

  ASSERT_OK(napi_define_properties(env, exports, countof(export_properties), export_properties));
  return exports;
}

NAPI_MODULE(libmpv, Init)
