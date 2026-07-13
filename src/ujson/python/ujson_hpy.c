#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "hpy.h"
#include "hpy/runtime/argparse.h"
#include "hpy/runtime/format.h"
#include "ultrajson.h"

static HPyGlobal g_json_decode_error = {0};
static HPyGlobal g_decimal_type = {0};
static HPyGlobal g_bytearray_type = {0};
static HPyGlobal g_memoryview_type = {0};

typedef struct {
  HPy handle;
} HpyJsonValue;

typedef struct {
  HPyContext *ctx;
} HpyDecoderState;

typedef struct {
  HPyContext *ctx;
  HPy default_fn;
} HpyEncoderState;

typedef struct {
  JSPFN_ITEREND iterEnd;
  JSPFN_ITERNEXT iterNext;
  JSPFN_ITERGETNAME iterGetName;
  JSPFN_ITERGETVALUE iterGetValue;
  HPy newObj;
  HPy utf8BytesObj;
  HPy dictObj;
  HPy_ssize_t index;
  HPy_ssize_t size;
  JSOBJ itemValue;
  HPy itemName;
  HPy rawJSONValue;

  union {
    JSINT64 longValue;
    JSUINT64 unsignedLongValue;
  };
} HpyEncoderTypeContext;

typedef char assert_wchar_t_is_jsuint32[1 - 2 * !(sizeof(wchar_t) == sizeof(JSUINT32))];

static HPy
hpy_loads_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
               HPy kwnames);

static HPy
hpy_loads_dispatch(HPyContext *ctx, HPy arg);

static HPy
hpy_dumps_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
               HPy kwnames);

static HPy
hpy_dump_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
              HPy kwnames);

static const char *
hpy_unicode_to_utf8_raw(HPyContext *ctx, HPy obj, size_t *out_len,
                        HPy *bytes_holder);

static void
set_decode_error(HPyContext *ctx, const char *message)
{
  HPy exc = HPyGlobal_Load(ctx, g_json_decode_error);
  if (HPy_IsNull(exc))
  {
    HPyErr_SetString(ctx, ctx->h_ValueError, message);
    return;
  }

  HPyErr_SetString(ctx, exc, message);
  HPy_Close(ctx, exc);
}

static HpyJsonValue *
hpy_json_value_new(HPyContext *ctx, HPy handle)
{
  HpyJsonValue *value = malloc(sizeof(HpyJsonValue));
  if (value == NULL)
  {
    HPy_Close(ctx, handle);
    HPyErr_NoMemory(ctx);
    return NULL;
  }

  value->handle = handle;
  return value;
}

static HPy
hpy_json_value_detach(HpyJsonValue *value)
{
  HPy handle = value->handle;
  value->handle = HPy_NULL;
  free(value);
  return handle;
}

static void
hpy_json_value_release(HPyContext *ctx, HpyJsonValue *value)
{
  if (value == NULL)
  {
    return;
  }

  HPy_Close(ctx, value->handle);
  free(value);
}

static HpyJsonValue *
hpy_json_value_from_jsobj(JSOBJ obj)
{
  return (HpyJsonValue *) obj;
}

static HPy
hpy_json_handle_from_jsobj(JSOBJ obj)
{
  return hpy_json_value_from_jsobj(obj)->handle;
}

static JSOBJ
hpy_json_value_dup_as_jsobj(HPyContext *ctx, HPy handle)
{
  return hpy_json_value_new(ctx, HPy_Dup(ctx, handle));
}

static void
hpy_json_value_release_jsobj(HPyContext *ctx, JSOBJ obj)
{
  hpy_json_value_release(ctx, hpy_json_value_from_jsobj(obj));
}

static JSOBJ
Decoder_newString(void *prv, JSUINT32 *start, JSUINT32 *end)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyUnicode_FromWideChar(
      state->ctx, (const wchar_t *) start, (HPy_ssize_t) (end - start));
  if (HPy_IsNull(handle))
  {
    return NULL;
  }

  return hpy_json_value_new(state->ctx, handle);
}

static void
Decoder_objectAddKey(void *prv, JSOBJ obj, JSOBJ name, JSOBJ value)
{
  HpyDecoderState *state = prv;
  HPyContext *ctx = state->ctx;
  int result = HPy_SetItem(
      ctx, hpy_json_handle_from_jsobj(obj), hpy_json_handle_from_jsobj(name),
      hpy_json_handle_from_jsobj(value));
  if (result < 0)
  {
    HPyErr_Clear(ctx);
    set_decode_error(ctx, "Invalid JSON: object keys must be strings");
  }

  hpy_json_value_release(ctx, hpy_json_value_from_jsobj(name));
  hpy_json_value_release(ctx, hpy_json_value_from_jsobj(value));
}

static void
Decoder_arrayAddItem(void *prv, JSOBJ obj, JSOBJ value)
{
  HpyDecoderState *state = prv;
  HPyContext *ctx = state->ctx;
  HPyList_Append(ctx, hpy_json_handle_from_jsobj(obj),
                 hpy_json_handle_from_jsobj(value));
  hpy_json_value_release(ctx, hpy_json_value_from_jsobj(value));
}

static JSOBJ
Decoder_newTrue(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_json_value_new(state->ctx, HPy_Dup(state->ctx, state->ctx->h_True));
}

static JSOBJ
Decoder_newFalse(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_json_value_new(state->ctx,
                            HPy_Dup(state->ctx, state->ctx->h_False));
}

static JSOBJ
Decoder_newNull(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_json_value_new(state->ctx, HPy_Dup(state->ctx, state->ctx->h_None));
}

static JSOBJ
Decoder_newNaN(void *prv)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyFloat_FromDouble(state->ctx, NAN);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newPosInf(void *prv)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyFloat_FromDouble(state->ctx, HUGE_VAL);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newNegInf(void *prv)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyFloat_FromDouble(state->ctx, -HUGE_VAL);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newObject(void *prv)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyDict_New(state->ctx);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newArray(void *prv)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyList_New(state->ctx, 0);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newInteger(void *prv, JSINT32 value)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyLong_FromLong(state->ctx, (long) value);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newLong(void *prv, JSINT64 value)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyLong_FromLongLong(state->ctx, (long long) value);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newUnsignedLong(void *prv, JSUINT64 value)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyLong_FromUnsignedLongLong(state->ctx,
                                            (unsigned long long) value);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static JSOBJ
Decoder_newIntegerFromString(void *prv, char *value, size_t length)
{
  HpyDecoderState *state = prv;
  HPy arg = HPy_NULL;
  HPy tuple = HPy_NULL;
  HPy result = HPy_NULL;
  HPy wrapped = HPy_NULL;

  char *buf = malloc(length + 1);
  if (buf == NULL)
  {
    HPyErr_NoMemory(state->ctx);
    return NULL;
  }

  memcpy(buf, value, length);
  buf[length] = '\0';

  arg = HPyUnicode_FromString(state->ctx, buf);
  free(buf);
  if (HPy_IsNull(arg))
  {
    return NULL;
  }

  tuple = HPyTuple_FromArray(state->ctx, &arg, 1);
  HPy_Close(state->ctx, arg);
  if (HPy_IsNull(tuple))
  {
    return NULL;
  }

  result = HPy_CallTupleDict(state->ctx, state->ctx->h_LongType, tuple, HPy_NULL);
  HPy_Close(state->ctx, tuple);
  if (HPy_IsNull(result))
  {
    return NULL;
  }

  wrapped = result;
  return hpy_json_value_new(state->ctx, wrapped);
}

static JSOBJ
Decoder_newDouble(void *prv, double value)
{
  HpyDecoderState *state = prv;
  HPy handle = HPyFloat_FromDouble(state->ctx, value);
  if (HPy_IsNull(handle))
  {
    return NULL;
  }
  return hpy_json_value_new(state->ctx, handle);
}

static void
Decoder_releaseObject(void *prv, JSOBJ obj)
{
  HpyDecoderState *state = prv;
  hpy_json_value_release(state->ctx, hpy_json_value_from_jsobj(obj));
}

static const char *loads_kwlist[] = {"obj", NULL};
static const char *load_kwlist[] = {"fp", NULL};

static HPy
hpy_loads_decode_raw(HPyContext *ctx, const char *raw, size_t raw_len)
{
  HpyDecoderState state = {.ctx = ctx};
  JSONObjectDecoder decoder = {
    Decoder_newString,
    Decoder_objectAddKey,
    Decoder_arrayAddItem,
    Decoder_newTrue,
    Decoder_newFalse,
    Decoder_newNull,
    Decoder_newNaN,
    Decoder_newPosInf,
    Decoder_newNegInf,
    Decoder_newObject,
    Decoder_newArray,
    Decoder_newInteger,
    Decoder_newLong,
    Decoder_newUnsignedLong,
    Decoder_newIntegerFromString,
    Decoder_newDouble,
    Decoder_releaseObject,
    malloc,
    free,
    realloc,
  };
  HpyJsonValue *ret;

  decoder.errorStr = NULL;
  decoder.errorOffset = NULL;
  decoder.prv = &state;
  decoder.s2d = NULL;

  dconv_s2d_init(&decoder.s2d, DCONV_S2D_ALLOW_TRAILING_JUNK, 0.0, 0.0,
                 "Infinity", "NaN");
  ret = hpy_json_value_from_jsobj(JSON_DecodeObject(&decoder, raw, raw_len));
  dconv_s2d_free(&decoder.s2d);

  if (HPyErr_Occurred(ctx))
  {
    if (ret != NULL)
    {
      hpy_json_value_release(ctx, ret);
    }
    return HPy_NULL;
  }

  if (decoder.errorStr != NULL)
  {
    set_decode_error(ctx, decoder.errorStr);
    if (ret != NULL)
    {
      hpy_json_value_release(ctx, ret);
    }
    return HPy_NULL;
  }

  if (ret == NULL)
  {
    HPyErr_SetString(ctx, ctx->h_RuntimeError, "JSON decoder returned NULL");
    return HPy_NULL;
  }

  return hpy_json_value_detach(ret);
}

static HPy
hpy_loads_decode_unicode(HPyContext *ctx, HPy arg)
{
  HPy bytes_holder = HPy_NULL;
  size_t raw_len = 0;
  const char *raw;

  raw = hpy_unicode_to_utf8_raw(ctx, arg, &raw_len, &bytes_holder);
  if (raw == NULL)
  {
    return HPy_NULL;
  }

  {
    HPy result = hpy_loads_decode_raw(ctx, raw, raw_len);
    HPy_Close(ctx, bytes_holder);
    return result;
  }
}

static HPy
hpy_loads_decode_bytes(HPyContext *ctx, HPy arg)
{
  HPy_ssize_t raw_len = HPyBytes_Size(ctx, arg);
  const char *raw;

  if (raw_len < 0)
  {
    return HPy_NULL;
  }

  raw = HPyBytes_AsString(ctx, arg);
  if (raw == NULL)
  {
    return HPy_NULL;
  }

  return hpy_loads_decode_raw(ctx, raw, (size_t) raw_len);
}

static HPy
hpy_loads_decode_buffer_like(HPyContext *ctx, HPy arg)
{
  HPy memoryview_type = HPyGlobal_Load(ctx, g_memoryview_type);
  HPy args_tuple = HPy_NULL;
  HPy view = HPy_NULL;
  HPy contiguous = HPy_NULL;
  HPy empty_args = HPy_NULL;
  HPy bytes_arg = HPy_NULL;
  int is_contiguous;

  if (HPy_IsNull(memoryview_type))
  {
    return HPy_NULL;
  }

  args_tuple = HPyTuple_Pack(ctx, 1, arg);
  if (HPy_IsNull(args_tuple))
  {
    HPy_Close(ctx, memoryview_type);
    return HPy_NULL;
  }

  view = HPy_CallTupleDict(ctx, memoryview_type, args_tuple, HPy_NULL);
  HPy_Close(ctx, args_tuple);
  HPy_Close(ctx, memoryview_type);
  if (HPy_IsNull(view))
  {
    HPyErr_Clear(ctx);
    return HPy_NULL;
  }

  contiguous = HPy_GetAttr_s(ctx, view, "c_contiguous");
  if (HPy_IsNull(contiguous))
  {
    HPy_Close(ctx, view);
    return HPy_NULL;
  }

  is_contiguous = HPy_IsTrue(ctx, contiguous);
  HPy_Close(ctx, contiguous);
  if (is_contiguous < 0)
  {
    HPy_Close(ctx, view);
    return HPy_NULL;
  }

  if (!is_contiguous)
  {
    HPy_Close(ctx, view);
    HPyErr_SetString(ctx, ctx->h_TypeError,
                     "Expected a C-contiguous bytes-like object");
    return HPy_NULL;
  }

  empty_args = HPyTuple_FromArray(ctx, NULL, 0);
  if (HPy_IsNull(empty_args))
  {
    HPy_Close(ctx, view);
    return HPy_NULL;
  }

  bytes_arg = HPy_CallMethodTupleDict_s(ctx, "tobytes", view, empty_args, HPy_NULL);
  HPy_Close(ctx, empty_args);
  HPy_Close(ctx, view);
  if (HPy_IsNull(bytes_arg))
  {
    return HPy_NULL;
  }

  {
    HPy result = hpy_loads_decode_bytes(ctx, bytes_arg);
    HPy_Close(ctx, bytes_arg);
    return result;
  }
}

static HPy
hpy_loads_dispatch(HPyContext *ctx, HPy arg)
{
  if (HPyUnicode_Check(ctx, arg))
  {
    return hpy_loads_decode_unicode(ctx, arg);
  }

  if (HPyBytes_Check(ctx, arg))
  {
    return hpy_loads_decode_bytes(ctx, arg);
  }

  {
    HPy bytearray_type = HPyGlobal_Load(ctx, g_bytearray_type);
    int is_bytearray = 0;

    if (!HPy_IsNull(bytearray_type))
    {
      is_bytearray = HPy_TypeCheck(ctx, arg, bytearray_type);
      HPy_Close(ctx, bytearray_type);
      if (is_bytearray)
      {
        HPy tuple = HPyTuple_Pack(ctx, 1, arg);
        HPy bytes_arg;
        if (HPy_IsNull(tuple))
        {
          return HPy_NULL;
        }

        bytes_arg = HPy_CallTupleDict(ctx, ctx->h_BytesType, tuple, HPy_NULL);
        HPy_Close(ctx, tuple);
        if (HPy_IsNull(bytes_arg))
        {
          return HPy_NULL;
        }

        {
          HPy result = hpy_loads_decode_bytes(ctx, bytes_arg);
          HPy_Close(ctx, bytes_arg);
          return result;
        }
      }
    }
  }

  {
    HPy result = hpy_loads_decode_buffer_like(ctx, arg);
    if (!HPy_IsNull(result))
    {
      return result;
    }
    if (HPyErr_Occurred(ctx))
    {
      return HPy_NULL;
    }
  }

  HPyErr_SetString(ctx, ctx->h_TypeError,
                   "Expected string, bytes, or bytearray");
  return HPy_NULL;
}

static HpyEncoderTypeContext *
hpy_encoder_type_context(JSONTypeContext *tc)
{
  return (HpyEncoderTypeContext *) tc->prv;
}

static HpyEncoderState *
hpy_encoder_state(JSONTypeContext *tc)
{
  return (HpyEncoderState *) tc->encoder_prv;
}

static HPy
hpy_encoder_get_obj_handle(JSOBJ obj, JSONTypeContext *tc)
{
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  if (!HPy_IsNull(etc->newObj))
  {
    return etc->newObj;
  }
  return hpy_json_handle_from_jsobj(obj);
}

static void
hpy_encoder_type_context_cleanup(HPyContext *ctx, HpyEncoderTypeContext *etc)
{
  if (etc == NULL)
  {
    return;
  }

  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }
  HPy_Close(ctx, etc->itemName);
  HPy_Close(ctx, etc->dictObj);
  HPy_Close(ctx, etc->utf8BytesObj);
  HPy_Close(ctx, etc->rawJSONValue);
  HPy_Close(ctx, etc->newObj);
  free(etc);
}

static HPy
hpy_unicode_encode_surrogatepass(HPyContext *ctx, HPy obj)
{
  HPy encoding = HPy_NULL;
  HPy errors = HPy_NULL;
  HPy args = HPy_NULL;
  HPy result = HPy_NULL;

  encoding = HPyUnicode_FromString(ctx, "utf-8");
  if (HPy_IsNull(encoding))
  {
    return HPy_NULL;
  }

  errors = HPyUnicode_FromString(ctx, "surrogatepass");
  if (HPy_IsNull(errors))
  {
    HPy_Close(ctx, encoding);
    return HPy_NULL;
  }

  args = HPyTuple_Pack(ctx, 2, encoding, errors);
  HPy_Close(ctx, encoding);
  HPy_Close(ctx, errors);
  if (HPy_IsNull(args))
  {
    return HPy_NULL;
  }

  result = HPy_CallMethodTupleDict_s(ctx, "encode", obj, args, HPy_NULL);
  HPy_Close(ctx, args);
  return result;
}

static const char *
hpy_unicode_to_utf8_raw(HPyContext *ctx, HPy obj, size_t *out_len, HPy *bytes_holder)
{
  HPy_ssize_t len = 0;
  const char *raw;

  HPy_Close(ctx, *bytes_holder);
  *bytes_holder = hpy_unicode_encode_surrogatepass(ctx, obj);
  if (HPy_IsNull(*bytes_holder))
  {
    return NULL;
  }

  len = HPyBytes_Size(ctx, *bytes_holder);
  if (len < 0)
  {
    HPy_Close(ctx, *bytes_holder);
    *bytes_holder = HPy_NULL;
    return NULL;
  }

  raw = HPyBytes_AsString(ctx, *bytes_holder);
  if (raw == NULL)
  {
    HPy_Close(ctx, *bytes_holder);
    *bytes_holder = HPy_NULL;
    return NULL;
  }

  *out_len = (size_t) len;
  return raw;
}

static const char *
hpy_bytes_to_utf8(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HPy value = hpy_encoder_get_obj_handle(obj, tc);
  HPy_ssize_t len = HPyBytes_Size(ctx, value);
  (void) out_value;
  if (len < 0)
  {
    return NULL;
  }

  *out_len = (size_t) len;
  return HPyBytes_AsString(ctx, value);
}

static const char *
hpy_unicode_to_utf8(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) out_value;
  return hpy_unicode_to_utf8_raw(ctx, hpy_encoder_get_obj_handle(obj, tc), out_len,
                                 &etc->utf8BytesObj);
}

static const char *
hpy_raw_json_to_utf8(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;
  (void) out_value;

  if (HPyUnicode_Check(ctx, etc->rawJSONValue))
  {
    return hpy_unicode_to_utf8_raw(ctx, etc->rawJSONValue, out_len, &etc->utf8BytesObj);
  }

  {
    HPy_ssize_t len = HPyBytes_Size(ctx, etc->rawJSONValue);
    if (len < 0)
    {
      return NULL;
    }
    *out_len = (size_t) len;
    return HPyBytes_AsString(ctx, etc->rawJSONValue);
  }
}

static void *
hpy_long_to_int64(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  (void) obj;
  (void) out_len;
  *((JSINT64 *) out_value) = hpy_encoder_type_context(tc)->longValue;
  return NULL;
}

static void *
hpy_long_to_uint64(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  (void) obj;
  (void) out_len;
  *((JSUINT64 *) out_value) = hpy_encoder_type_context(tc)->unsignedLongValue;
  return NULL;
}

static void *
hpy_float_to_double(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  (void) out_len;
  *((double *) out_value) = HPyFloat_AsDouble(ctx, hpy_encoder_get_obj_handle(obj, tc));
  return NULL;
}

static int
hpy_object_is_decimal_type(HPyContext *ctx, HPy obj)
{
  HPy decimal_type = HPyGlobal_Load(ctx, g_decimal_type);
  int result = 0;

  if (HPy_IsNull(decimal_type))
  {
    return 0;
  }

  result = HPy_TypeCheck(ctx, obj, decimal_type);
  HPy_Close(ctx, decimal_type);
  return result;
}

static int
hpy_object_is_float_type(HPyContext *ctx, HPy obj)
{
  HPy type = HPy_Type(ctx, obj);
  int result = 0;

  if (HPy_IsNull(type))
  {
    return 0;
  }

  result = HPyType_IsSubtype(ctx, type, ctx->h_FloatType);
  HPy_Close(ctx, type);
  return result;
}

static HPy
hpy_dict_convert_key(HPyContext *ctx, HPy key)
{
  if (HPyUnicode_Check(ctx, key))
  {
    return hpy_unicode_encode_surrogatepass(ctx, key);
  }

  if (HPyBytes_Check(ctx, key))
  {
    return HPy_Dup(ctx, key);
  }

  if (HPy_Is(ctx, key, ctx->h_True))
  {
    return HPyBytes_FromString(ctx, "true");
  }

  if (HPy_Is(ctx, key, ctx->h_False))
  {
    return HPyBytes_FromString(ctx, "false");
  }

  if (HPy_Is(ctx, key, ctx->h_None))
  {
    return HPyBytes_FromString(ctx, "null");
  }

  {
    HPy key_str = HPy_Str(ctx, key);
    HPy result;
    if (HPy_IsNull(key_str))
    {
      return HPy_NULL;
    }
    result = hpy_unicode_encode_surrogatepass(ctx, key_str);
    HPy_Close(ctx, key_str);
    return result;
  }
}

static int
hpy_tuple_iter_next(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy tuple = hpy_encoder_get_obj_handle(obj, tc);
  HPy item;

  if (etc->index >= etc->size)
  {
    return 0;
  }

  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }

  item = HPy_GetItem_i(ctx, tuple, etc->index);
  if (HPy_IsNull(item))
  {
    return -1;
  }

  etc->itemValue = hpy_json_value_new(ctx, item);
  if (etc->itemValue == NULL)
  {
    return -1;
  }

  etc->index += 1;
  return 1;
}

static void
hpy_tuple_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;
  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }
}

static JSOBJ
hpy_tuple_iter_get_value(JSOBJ obj, JSONTypeContext *tc)
{
  (void) obj;
  return hpy_encoder_type_context(tc)->itemValue;
}

static int
hpy_list_iter_next(JSOBJ obj, JSONTypeContext *tc)
{
  return hpy_tuple_iter_next(obj, tc);
}

static void
hpy_list_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  hpy_tuple_iter_end(obj, tc);
}

static JSOBJ
hpy_list_iter_get_value(JSOBJ obj, JSONTypeContext *tc)
{
  return hpy_tuple_iter_get_value(obj, tc);
}

static int
hpy_dict_iter_next(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy dict = etc->dictObj;
  HPy keys = HPyDict_Keys(ctx, dict);
  HPy key = HPy_NULL;
  HPy value = HPy_NULL;
  int result = 0;

  (void) obj;

  if (HPy_IsNull(keys))
  {
    return -1;
  }

  etc->size = HPy_Length(ctx, keys);
  if (etc->size < 0)
  {
    HPy_Close(ctx, keys);
    return -1;
  }

  if (etc->index >= etc->size)
  {
    HPy_Close(ctx, keys);
    return 0;
  }

  key = HPy_GetItem_i(ctx, keys, etc->index);
  HPy_Close(ctx, keys);
  if (HPy_IsNull(key))
  {
    return -1;
  }

  value = HPy_GetItem(ctx, dict, key);
  if (HPy_IsNull(value))
  {
    HPy_Close(ctx, key);
    return -1;
  }

  HPy_Close(ctx, etc->itemName);
  etc->itemName = hpy_dict_convert_key(ctx, key);
  if (HPy_IsNull(etc->itemName))
  {
    HPy_Close(ctx, value);
    HPy_Close(ctx, key);
    return -1;
  }

  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }

  etc->itemValue = hpy_json_value_new(ctx, value);
  if (etc->itemValue == NULL)
  {
    HPy_Close(ctx, key);
    return -1;
  }

  HPy_Close(ctx, key);
  etc->index += 1;
  result = 1;
  return result;
}

static int
hpy_sorted_dict_iter_next(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy empty_args = HPy_NULL;
  HPy key = HPy_NULL;
  HPy value = HPy_NULL;

  (void) obj;

  if (HPy_IsNull(etc->newObj))
  {
    HPy sort_result = HPy_NULL;
    etc->newObj = HPyDict_Keys(ctx, etc->dictObj);
    if (HPy_IsNull(etc->newObj))
    {
      return -1;
    }

    empty_args = HPyTuple_FromArray(ctx, NULL, 0);
    if (HPy_IsNull(empty_args))
    {
      return -1;
    }

    sort_result = HPy_CallMethodTupleDict_s(ctx, "sort", etc->newObj, empty_args,
                                            HPy_NULL);
    if (HPy_IsNull(sort_result))
    {
      HPy_Close(ctx, empty_args);
      return -1;
    }
    HPy_Close(ctx, sort_result);
    HPy_Close(ctx, empty_args);

    etc->size = HPy_Length(ctx, etc->newObj);
    if (etc->size < 0)
    {
      return -1;
    }
  }

  if (etc->index >= etc->size)
  {
    return 0;
  }

  key = HPy_GetItem_i(ctx, etc->newObj, etc->index);
  if (HPy_IsNull(key))
  {
    return -1;
  }

  value = HPy_GetItem(ctx, etc->dictObj, key);
  if (HPy_IsNull(value))
  {
    HPy_Close(ctx, key);
    return -1;
  }

  HPy_Close(ctx, etc->itemName);
  etc->itemName = hpy_dict_convert_key(ctx, key);
  if (HPy_IsNull(etc->itemName))
  {
    HPy_Close(ctx, value);
    HPy_Close(ctx, key);
    return -1;
  }

  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }

  etc->itemValue = hpy_json_value_new(ctx, value);
  if (etc->itemValue == NULL)
  {
    HPy_Close(ctx, key);
    return -1;
  }

  HPy_Close(ctx, key);
  etc->index += 1;
  return 1;
}

static void
hpy_dict_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;

  if (etc->itemValue != NULL)
  {
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
    etc->itemValue = NULL;
  }
  HPy_Close(ctx, etc->itemName);
  etc->itemName = HPy_NULL;
  HPy_Close(ctx, etc->dictObj);
  etc->dictObj = HPy_NULL;
}

static JSOBJ
hpy_dict_iter_get_value(JSOBJ obj, JSONTypeContext *tc)
{
  (void) obj;
  return hpy_encoder_type_context(tc)->itemValue;
}

static char *
hpy_dict_iter_get_name(JSOBJ obj, JSONTypeContext *tc, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy_ssize_t len = HPyBytes_Size(ctx, etc->itemName);
  (void) obj;
  if (len < 0)
  {
    return NULL;
  }
  *out_len = (size_t) len;
  return (char *) HPyBytes_AsString(ctx, etc->itemName);
}

static int
hpy_setup_dict_iter(HPyContext *ctx, HPy dict_obj, HpyEncoderTypeContext *etc,
                    JSONObjectEncoder *enc)
{
  etc->dictObj = HPy_Dup(ctx, dict_obj);
  if (HPy_IsNull(etc->dictObj))
  {
    return -1;
  }

  etc->iterNext = enc->sortKeys ? hpy_sorted_dict_iter_next : hpy_dict_iter_next;
  etc->iterEnd = hpy_dict_iter_end;
  etc->iterGetValue = hpy_dict_iter_get_value;
  etc->iterGetName = hpy_dict_iter_get_name;
  etc->index = 0;
  etc->size = 0;
  return 0;
}

static void
hpy_encoder_begin_type_context(JSOBJ obj, JSONTypeContext *tc, JSONObjectEncoder *enc)
{
  HpyEncoderState *state = (HpyEncoderState *) tc->encoder_prv;
  HPyContext *ctx = state->ctx;
  HpyEncoderTypeContext *etc;
  HPy value = HPy_NULL;
  int level = 0;

  tc->prv = malloc(sizeof(HpyEncoderTypeContext));
  etc = hpy_encoder_type_context(tc);
  if (etc == NULL)
  {
    tc->type = JT_INVALID;
    HPyErr_NoMemory(ctx);
    return;
  }

  memset(etc, 0, sizeof(*etc));
  etc->newObj = HPy_NULL;
  etc->utf8BytesObj = HPy_NULL;
  etc->dictObj = HPy_NULL;
  etc->itemName = HPy_NULL;
  etc->rawJSONValue = HPy_NULL;

  if (obj == NULL)
  {
    tc->type = JT_INVALID;
    hpy_encoder_type_context_cleanup(ctx, etc);
    tc->prv = NULL;
    return;
  }

  value = hpy_json_handle_from_jsobj(obj);

BEGIN:
  if (HPy_Is(ctx, value, ctx->h_True))
  {
    tc->type = JT_TRUE;
    return;
  }

  if (HPy_Is(ctx, value, ctx->h_False))
  {
    tc->type = JT_FALSE;
    return;
  }

  if (HPy_TypeCheck(ctx, value, ctx->h_LongType))
  {
    etc->longValue = (JSINT64) HPyLong_AsLongLong(ctx, value);
    if (!(etc->longValue == -1 && HPyErr_Occurred(ctx)))
    {
      tc->type = JT_LONG;
      return;
    }

    if (!HPyErr_ExceptionMatches(ctx, ctx->h_OverflowError))
    {
      goto INVALID;
    }
    HPyErr_Clear(ctx);

    etc->unsignedLongValue = (JSUINT64) HPyLong_AsUnsignedLongLong(ctx, value);
    if (!(etc->unsignedLongValue == (unsigned long long) -1 &&
          HPyErr_Occurred(ctx)))
    {
      tc->type = JT_ULONG;
      return;
    }

    if (!HPyErr_ExceptionMatches(ctx, ctx->h_OverflowError))
    {
      goto INVALID;
    }
    HPyErr_Clear(ctx);

    etc->rawJSONValue = HPy_Str(ctx, value);
    if (HPy_IsNull(etc->rawJSONValue))
    {
      goto INVALID;
    }
    tc->type = JT_RAW;
    return;
  }

  if (HPyBytes_Check(ctx, value))
  {
    if (enc->rejectBytes)
    {
      HPyErr_Format(ctx, ctx->h_TypeError, "reject_bytes is on and bytes is bytes");
      goto INVALID;
    }
    tc->type = JT_UTF8;
    return;
  }

  if (HPyUnicode_Check(ctx, value))
  {
    tc->type = JT_UTF8;
    return;
  }

  if (HPy_Is(ctx, value, ctx->h_None))
  {
    tc->type = JT_NULL;
    return;
  }

  if (hpy_object_is_float_type(ctx, value) ||
      hpy_object_is_decimal_type(ctx, value))
  {
    tc->type = JT_DOUBLE;
    return;
  }

  if (HPyDict_Check(ctx, value))
  {
    if (hpy_setup_dict_iter(ctx, value, etc, enc) < 0)
    {
      goto INVALID;
    }
    tc->type = JT_OBJECT;
    return;
  }

  if (HPyList_Check(ctx, value))
  {
    etc->iterEnd = hpy_list_iter_end;
    etc->iterNext = hpy_list_iter_next;
    etc->iterGetValue = hpy_list_iter_get_value;
    etc->index = 0;
    etc->size = HPy_Length(ctx, value);
    if (etc->size < 0)
    {
      goto INVALID;
    }
    tc->type = JT_ARRAY;
    return;
  }

  if (HPyTuple_Check(ctx, value))
  {
    etc->iterEnd = hpy_tuple_iter_end;
    etc->iterNext = hpy_tuple_iter_next;
    etc->iterGetValue = hpy_tuple_iter_get_value;
    etc->index = 0;
    etc->size = HPy_Length(ctx, value);
    if (etc->size < 0)
    {
      goto INVALID;
    }
    tc->type = JT_ARRAY;
    return;
  }

  {
    int has_to_dict = HPy_HasAttr_s(ctx, value, "toDict");
    if (has_to_dict < 0)
    {
      goto INVALID;
    }
    if (has_to_dict)
    {
      HPy empty_args = HPyTuple_FromArray(ctx, NULL, 0);
      HPy to_dict_result;
      if (HPy_IsNull(empty_args))
      {
        goto INVALID;
      }
      to_dict_result = HPy_CallMethodTupleDict_s(ctx, "toDict", value, empty_args,
                                                 HPy_NULL);
      HPy_Close(ctx, empty_args);
      if (HPy_IsNull(to_dict_result))
      {
        goto INVALID;
      }
      if (!HPyDict_Check(ctx, to_dict_result))
      {
        HPy_Close(ctx, to_dict_result);
        HPyErr_SetString(ctx, ctx->h_TypeError,
                         "toDict() should return a dict");
        goto INVALID;
      }
      if (hpy_setup_dict_iter(ctx, to_dict_result, etc, enc) < 0)
      {
        HPy_Close(ctx, to_dict_result);
        goto INVALID;
      }
      HPy_Close(ctx, to_dict_result);
      tc->type = JT_OBJECT;
      return;
    }
  }

  {
    int has_json = HPy_HasAttr_s(ctx, value, "__json__");
    if (has_json < 0)
    {
      goto INVALID;
    }
    if (has_json)
    {
      HPy empty_args = HPyTuple_FromArray(ctx, NULL, 0);
      if (HPy_IsNull(empty_args))
      {
        goto INVALID;
      }
      etc->rawJSONValue =
          HPy_CallMethodTupleDict_s(ctx, "__json__", value, empty_args, HPy_NULL);
      HPy_Close(ctx, empty_args);
      if (HPy_IsNull(etc->rawJSONValue))
      {
        goto INVALID;
      }
      if (!HPyBytes_Check(ctx, etc->rawJSONValue) &&
          !HPyUnicode_Check(ctx, etc->rawJSONValue))
      {
        HPyErr_SetString(ctx, ctx->h_TypeError,
                         "__json__() should return str or bytes");
        goto INVALID;
      }
      tc->type = JT_RAW;
      return;
    }
  }

  if (!HPy_IsNull(state->default_fn) && !HPy_Is(ctx, state->default_fn, ctx->h_None))
  {
    HPy new_obj;

    if (level >= 3)
    {
      HPyErr_SetString(ctx, ctx->h_TypeError, "maximum recursion depth exceeded");
      goto INVALID;
    }

    {
      HPy args = HPyTuple_Pack(ctx, 1, value);
      if (HPy_IsNull(args))
      {
        goto INVALID;
      }
      new_obj = HPy_CallTupleDict(ctx, state->default_fn, args, HPy_NULL);
      HPy_Close(ctx, args);
    }
    if (HPy_IsNull(new_obj))
    {
      goto INVALID;
    }

    HPy_Close(ctx, etc->newObj);
    etc->newObj = new_obj;
    value = etc->newObj;
    level += 1;
    goto BEGIN;
  }

  {
    HPy repr = HPy_Repr(ctx, value);
    HPy repr_bytes = HPy_NULL;
    const char *repr_raw = NULL;
    if (HPy_IsNull(repr))
    {
      goto INVALID;
    }

    repr_bytes = HPyUnicode_AsUTF8String(ctx, repr);
    HPy_Close(ctx, repr);
    if (!HPy_IsNull(repr_bytes))
    {
      repr_raw = HPyBytes_AsString(ctx, repr_bytes);
      if (repr_raw != NULL)
      {
        HPyErr_Format(ctx, ctx->h_TypeError, "%s is not JSON serializable",
                      repr_raw);
      }
      HPy_Close(ctx, repr_bytes);
    }
  }

INVALID:
  tc->type = JT_INVALID;
  hpy_encoder_type_context_cleanup(ctx, etc);
  tc->prv = NULL;
}

static void
hpy_encoder_end_type_context(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;
  if (etc == NULL)
  {
    return;
  }
  hpy_encoder_type_context_cleanup(ctx, etc);
  tc->prv = NULL;
}

static const char *
hpy_encoder_get_string_value(JSOBJ obj, JSONTypeContext *tc, size_t *out_len)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HPy value = hpy_encoder_get_obj_handle(obj, tc);

  if (tc->type == JT_RAW)
  {
    return hpy_raw_json_to_utf8(obj, tc, NULL, out_len);
  }

  if (HPyBytes_Check(ctx, value))
  {
    return hpy_bytes_to_utf8(obj, tc, NULL, out_len);
  }

  return hpy_unicode_to_utf8(obj, tc, NULL, out_len);
}

static JSINT64
hpy_encoder_get_long_value(JSOBJ obj, JSONTypeContext *tc)
{
  JSINT64 value = 0;
  hpy_long_to_int64(obj, tc, &value, NULL);
  return value;
}

static JSUINT64
hpy_encoder_get_unsigned_long_value(JSOBJ obj, JSONTypeContext *tc)
{
  JSUINT64 value = 0;
  hpy_long_to_uint64(obj, tc, &value, NULL);
  return value;
}

static double
hpy_encoder_get_double_value(JSOBJ obj, JSONTypeContext *tc)
{
  double value = 0.0;
  hpy_float_to_double(obj, tc, &value, NULL);
  return value;
}

static int
hpy_encoder_iter_next(JSOBJ obj, JSONTypeContext *tc)
{
  return hpy_encoder_type_context(tc)->iterNext(obj, tc);
}

static void
hpy_encoder_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  hpy_encoder_type_context(tc)->iterEnd(obj, tc);
}

static JSOBJ
hpy_encoder_iter_get_value(JSOBJ obj, JSONTypeContext *tc)
{
  return hpy_encoder_type_context(tc)->iterGetValue(obj, tc);
}

static char *
hpy_encoder_iter_get_name(JSOBJ obj, JSONTypeContext *tc, size_t *out_len)
{
  return hpy_encoder_type_context(tc)->iterGetName(obj, tc, out_len);
}

static const char *dumps_kwlist[] = {
  "obj",
  "ensure_ascii",
  "encode_html_chars",
  "escape_forward_slashes",
  "sort_keys",
  "indent",
  "allow_nan",
  "reject_bytes",
  "default",
  "separators",
  NULL
};

static const char *dump_kwlist[] = {
  "obj",
  "fp",
  "ensure_ascii",
  "encode_html_chars",
  "escape_forward_slashes",
  "sort_keys",
  "indent",
  "allow_nan",
  "reject_bytes",
  "default",
  "separators",
  NULL
};

static HPy
hpy_encode_python_object(HPyContext *ctx, HPy input, HPy default_fn,
                         int force_ascii, int encode_html_chars,
                         int escape_forward_slashes, int sort_keys, int indent,
                         int allow_nan, int reject_bytes, HPy separators)
{
  char buffer[65536];
  char *ret;
  size_t ret_len = 0;
  const char *cs_nan = NULL;
  const char *cs_inf = NULL;
  HPy item_sep_bytes = HPy_NULL;
  HPy key_sep_bytes = HPy_NULL;
  HPy encoded_result = HPy_NULL;
  JSOBJ root = NULL;
  HPy result = HPy_NULL;
  HpyEncoderState state = {
    .ctx = ctx,
    .default_fn = default_fn,
  };
  JSONObjectEncoder encoder = {0};

  encoder.beginTypeContext = hpy_encoder_begin_type_context;
  encoder.endTypeContext = hpy_encoder_end_type_context;
  encoder.getStringValue = hpy_encoder_get_string_value;
  encoder.getLongValue = hpy_encoder_get_long_value;
  encoder.getUnsignedLongValue = hpy_encoder_get_unsigned_long_value;
  encoder.getDoubleValue = hpy_encoder_get_double_value;
  encoder.iterNext = hpy_encoder_iter_next;
  encoder.iterEnd = hpy_encoder_iter_end;
  encoder.iterGetValue = hpy_encoder_iter_get_value;
  encoder.iterGetName = hpy_encoder_iter_get_name;
  encoder.malloc = malloc;
  encoder.realloc = realloc;
  encoder.free = free;
  encoder.recursionMax = -1;
  encoder.forceASCII = force_ascii;
  encoder.encodeHTMLChars = encode_html_chars;
  encoder.escapeForwardSlashes = escape_forward_slashes;
  encoder.sortKeys = sort_keys;
  encoder.indent = indent;
  encoder.allowNan = allow_nan;
  encoder.rejectBytes = reject_bytes;
  encoder.prv = &state;

  if (allow_nan)
  {
    cs_inf = "Infinity";
    cs_nan = "NaN";
  }

  if (!HPy_IsNull(separators) && !HPy_Is(ctx, separators, ctx->h_None))
  {
    HPy item_sep = HPy_NULL;
    HPy key_sep = HPy_NULL;

    if (!HPyTuple_Check(ctx, separators))
    {
      HPyErr_SetString(ctx, ctx->h_TypeError,
                       "expected tuple or None as separator");
      return HPy_NULL;
    }

    if (HPy_Length(ctx, separators) != 2)
    {
      HPyErr_SetString(ctx, ctx->h_ValueError,
                       "expected tuple of size 2 as separator");
      return HPy_NULL;
    }

    item_sep = HPy_GetItem_i(ctx, separators, 0);
    if (HPy_IsNull(item_sep))
    {
      return HPy_NULL;
    }
    key_sep = HPy_GetItem_i(ctx, separators, 1);
    if (HPy_IsNull(key_sep))
    {
      HPy_Close(ctx, item_sep);
      return HPy_NULL;
    }

    if (!HPyUnicode_Check(ctx, item_sep))
    {
      HPy_Close(ctx, item_sep);
      HPy_Close(ctx, key_sep);
      HPyErr_SetString(ctx, ctx->h_TypeError, "expected str as item separator");
      return HPy_NULL;
    }
    if (!HPyUnicode_Check(ctx, key_sep))
    {
      HPy_Close(ctx, item_sep);
      HPy_Close(ctx, key_sep);
      HPyErr_SetString(ctx, ctx->h_TypeError, "expected str as key separator");
      return HPy_NULL;
    }

    encoder.itemSeparatorChars =
        hpy_unicode_to_utf8_raw(ctx, item_sep, &encoder.itemSeparatorLength,
                                &item_sep_bytes);
    HPy_Close(ctx, item_sep);
    if (encoder.itemSeparatorChars == NULL)
    {
      HPy_Close(ctx, key_sep);
      return HPy_NULL;
    }

    encoder.keySeparatorChars =
        hpy_unicode_to_utf8_raw(ctx, key_sep, &encoder.keySeparatorLength,
                                &key_sep_bytes);
    HPy_Close(ctx, key_sep);
    if (encoder.keySeparatorChars == NULL)
    {
      HPy_Close(ctx, item_sep_bytes);
      return HPy_NULL;
    }
  }
  else
  {
    encoder.itemSeparatorChars = ",";
    encoder.itemSeparatorLength = 1;
    if (indent)
    {
      encoder.keySeparatorChars = ": ";
      encoder.keySeparatorLength = 2;
    }
    else
    {
      encoder.keySeparatorChars = ":";
      encoder.keySeparatorLength = 1;
    }
  }

  dconv_d2s_init(&encoder.d2s,
                 DCONV_D2S_EMIT_TRAILING_DECIMAL_POINT |
                     DCONV_D2S_EMIT_TRAILING_ZERO_AFTER_POINT |
                     DCONV_D2S_EMIT_POSITIVE_EXPONENT_SIGN,
                 cs_inf, cs_nan, 'e', DCONV_DECIMAL_IN_SHORTEST_LOW,
                 DCONV_DECIMAL_IN_SHORTEST_HIGH, 0, 0);

  root = hpy_json_value_dup_as_jsobj(ctx, input);
  if (root == NULL)
  {
    dconv_d2s_free(&encoder.d2s);
    HPy_Close(ctx, item_sep_bytes);
    HPy_Close(ctx, key_sep_bytes);
    return HPy_NULL;
  }

  ret = JSON_EncodeObject(root, &encoder, buffer, sizeof(buffer), &ret_len);
  hpy_json_value_release_jsobj(ctx, root);

  dconv_d2s_free(&encoder.d2s);
  HPy_Close(ctx, item_sep_bytes);
  HPy_Close(ctx, key_sep_bytes);

  if (encoder.errorMsg != NULL)
  {
    if (!HPyErr_Occurred(ctx))
    {
      HPyErr_Format(ctx, ctx->h_OverflowError, "%s", encoder.errorMsg);
    }
    if (ret != NULL && ret != buffer)
    {
      encoder.free(ret);
    }
    return HPy_NULL;
  }

  if (HPyErr_Occurred(ctx))
  {
    if (ret != NULL && ret != buffer)
    {
      encoder.free(ret);
    }
    return HPy_NULL;
  }

  encoded_result = HPyBytes_FromStringAndSize(ctx, ret, (HPy_ssize_t) ret_len);
  if (ret != buffer)
  {
    encoder.free(ret);
  }
  if (HPy_IsNull(encoded_result))
  {
    return HPy_NULL;
  }
  result = HPyUnicode_FromEncodedObject(ctx, encoded_result, "utf-8",
                                        "surrogatepass");
  HPy_Close(ctx, encoded_result);
  return result;
}

HPyDef_METH(hpy_encode, "encode", HPyFunc_KEYWORDS)
static HPy
hpy_encode_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
                HPy kwnames)
{
  return hpy_dumps_impl(ctx, self, args, nargs, kwnames);
}

HPyDef_METH(hpy_decode, "decode", HPyFunc_KEYWORDS)
static HPy
hpy_decode_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
                HPy kwnames)
{
  return hpy_loads_impl(ctx, self, args, nargs, kwnames);
}

HPyDef_METH(hpy_dumps, "dumps", HPyFunc_KEYWORDS)
static HPy
hpy_dumps_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
               HPy kwnames)
{
  HPyTracker ht = {0};
  HPy input;
  HPy ensure_ascii = HPy_NULL;
  HPy encode_html_chars = HPy_NULL;
  HPy escape_forward_slashes = HPy_NULL;
  HPy sort_keys = HPy_NULL;
  HPy default_fn = HPy_NULL;
  HPy separators = HPy_NULL;
  int indent = 0;
  int allow_nan = 1;
  int reject_bytes = 1;
  int force_ascii = 1;
  int encode_html = 0;
  int escape_slashes = 1;
  int sort = 0;

  (void) self;

  if (!HPyArg_ParseKeywords(ctx, &ht, args, nargs, kwnames, "O|OOOOiiiOO",
                            dumps_kwlist, &input, &ensure_ascii,
                            &encode_html_chars, &escape_forward_slashes,
                            &sort_keys, &indent, &allow_nan, &reject_bytes,
                            &default_fn, &separators))
  {
    if (ht._i != 0)
    {
      HPyTracker_Close(ctx, ht);
    }
    return HPy_NULL;
  }

  if (!HPy_IsNull(ensure_ascii))
  {
    force_ascii = HPy_IsTrue(ctx, ensure_ascii);
    if (force_ascii < 0)
    {
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(encode_html_chars))
  {
    encode_html = HPy_IsTrue(ctx, encode_html_chars);
    if (encode_html < 0)
    {
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(escape_forward_slashes))
  {
    escape_slashes = HPy_IsTrue(ctx, escape_forward_slashes);
    if (escape_slashes < 0)
    {
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(sort_keys))
  {
    sort = HPy_IsTrue(ctx, sort_keys);
    if (sort < 0)
    {
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (indent < -1)
  {
    indent = -1;
  }
  else if (indent > 1000)
  {
    HPyTracker_Close(ctx, ht);
    HPyErr_SetString(ctx, ctx->h_ValueError,
                     "Maximum allowed indentation is 1000");
    return HPy_NULL;
  }

  {
    HPy result = hpy_encode_python_object(
        ctx, input, default_fn, force_ascii, encode_html, escape_slashes, sort,
        indent, allow_nan, reject_bytes, separators);
    HPyTracker_Close(ctx, ht);
    return result;
  }
}

HPyDef_METH(hpy_loads, "loads", HPyFunc_KEYWORDS)
static HPy
hpy_loads_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
               HPy kwnames)
{
  HPyTracker ht = {0};
  HPy arg;

  if (!HPyArg_ParseKeywords(ctx, &ht, args, nargs, kwnames, "O", loads_kwlist,
                            &arg))
  {
    if (ht._i != 0)
    {
      HPyTracker_Close(ctx, ht);
    }
    return HPy_NULL;
  }

  {
    HPy result = hpy_loads_dispatch(ctx, arg);
    HPyTracker_Close(ctx, ht);
    return result;
  }
}

HPyDef_METH(hpy_dump, "dump", HPyFunc_KEYWORDS)
static HPy
hpy_dump_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
              HPy kwnames)
{
  HPyTracker ht = {0};
  HPy input;
  HPy file;
  HPy ensure_ascii = HPy_NULL;
  HPy encode_html_chars = HPy_NULL;
  HPy escape_forward_slashes = HPy_NULL;
  HPy sort_keys = HPy_NULL;
  HPy default_fn = HPy_NULL;
  HPy separators = HPy_NULL;
  HPy write = HPy_NULL;
  HPy string = HPy_NULL;
  HPy args_tuple = HPy_NULL;
  HPy write_result = HPy_NULL;
  int indent = 0;
  int allow_nan = 1;
  int reject_bytes = 1;
  int force_ascii = 1;
  int encode_html = 0;
  int escape_slashes = 1;
  int sort = 0;
  int has_write;

  (void) self;

  if (!HPyArg_ParseKeywords(ctx, &ht, args, nargs, kwnames, "OO|OOOOiiiOO",
                            dump_kwlist, &input, &file, &ensure_ascii,
                            &encode_html_chars, &escape_forward_slashes,
                            &sort_keys, &indent, &allow_nan, &reject_bytes,
                            &default_fn, &separators))
  {
    if (ht._i != 0)
    {
      HPyTracker_Close(ctx, ht);
    }
    return HPy_NULL;
  }

  has_write = HPy_HasAttr_s(ctx, file, "write");
  if (has_write < 0)
  {
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }
  if (!has_write)
  {
    HPyTracker_Close(ctx, ht);
    HPyErr_SetString(ctx, ctx->h_TypeError, "expected file");
    return HPy_NULL;
  }

  write = HPy_GetAttr_s(ctx, file, "write");
  if (HPy_IsNull(write))
  {
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }
  if (!HPyCallable_Check(ctx, write))
  {
    HPy_Close(ctx, write);
    HPyTracker_Close(ctx, ht);
    HPyErr_SetString(ctx, ctx->h_TypeError, "expected file");
    return HPy_NULL;
  }

  if (!HPy_IsNull(ensure_ascii))
  {
    force_ascii = HPy_IsTrue(ctx, ensure_ascii);
    if (force_ascii < 0)
    {
      HPy_Close(ctx, write);
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(encode_html_chars))
  {
    encode_html = HPy_IsTrue(ctx, encode_html_chars);
    if (encode_html < 0)
    {
      HPy_Close(ctx, write);
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(escape_forward_slashes))
  {
    escape_slashes = HPy_IsTrue(ctx, escape_forward_slashes);
    if (escape_slashes < 0)
    {
      HPy_Close(ctx, write);
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (!HPy_IsNull(sort_keys))
  {
    sort = HPy_IsTrue(ctx, sort_keys);
    if (sort < 0)
    {
      HPy_Close(ctx, write);
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }
  }

  if (indent < -1)
  {
    indent = -1;
  }
  else if (indent > 1000)
  {
    HPy_Close(ctx, write);
    HPyTracker_Close(ctx, ht);
    HPyErr_SetString(ctx, ctx->h_ValueError,
                     "Maximum allowed indentation is 1000");
    return HPy_NULL;
  }

  string = hpy_encode_python_object(ctx, input, default_fn, force_ascii,
                                    encode_html, escape_slashes, sort, indent,
                                    allow_nan, reject_bytes, separators);
  if (HPy_IsNull(string))
  {
    HPy_Close(ctx, write);
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }

  args_tuple = HPyTuple_Pack(ctx, 1, string);
  if (HPy_IsNull(args_tuple))
  {
    HPy_Close(ctx, string);
    HPy_Close(ctx, write);
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }

  write_result = HPy_CallTupleDict(ctx, write, args_tuple, HPy_NULL);
  HPy_Close(ctx, args_tuple);
  HPy_Close(ctx, string);
  HPy_Close(ctx, write);
  if (HPy_IsNull(write_result))
  {
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }
  HPy_Close(ctx, write_result);
  HPyTracker_Close(ctx, ht);
  return HPy_Dup(ctx, ctx->h_None);
}

HPyDef_METH(hpy_load, "load", HPyFunc_KEYWORDS)
static HPy
hpy_load_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
              HPy kwnames)
{
  HPyTracker ht = {0};
  HPy file;
  HPy read = HPy_NULL;
  HPy empty_args = HPy_NULL;
  HPy string = HPy_NULL;

  if (!HPyArg_ParseKeywords(ctx, &ht, args, nargs, kwnames, "O", load_kwlist,
                            &file))
  {
    if (ht._i != 0)
    {
      HPyTracker_Close(ctx, ht);
    }
    return HPy_NULL;
  }

  {
    int has_read = HPy_HasAttr_s(ctx, file, "read");
    if (has_read < 0)
    {
      HPyTracker_Close(ctx, ht);
      return HPy_NULL;
    }

    if (!has_read)
    {
      HPyTracker_Close(ctx, ht);
      HPyErr_SetString(ctx, ctx->h_TypeError, "expected file");
      return HPy_NULL;
    }
  }

  read = HPy_GetAttr_s(ctx, file, "read");
  if (HPy_IsNull(read))
  {
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }

  if (!HPyCallable_Check(ctx, read))
  {
    HPy_Close(ctx, read);
    HPyTracker_Close(ctx, ht);
    HPyErr_SetString(ctx, ctx->h_TypeError, "expected file");
    return HPy_NULL;
  }

  empty_args = HPyTuple_FromArray(ctx, NULL, 0);
  if (HPy_IsNull(empty_args))
  {
    HPy_Close(ctx, read);
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }

  string = HPy_CallTupleDict(ctx, read, empty_args, HPy_NULL);
  HPy_Close(ctx, read);
  HPy_Close(ctx, empty_args);
  if (HPy_IsNull(string))
  {
    HPyTracker_Close(ctx, ht);
    return HPy_NULL;
  }

  {
    HPy result = hpy_loads_dispatch(ctx, string);
    HPy_Close(ctx, string);
    HPyTracker_Close(ctx, ht);
    return result;
  }
}

HPyDef_METH(hpy_abi, "_hpy_abi", HPyFunc_NOARGS)
static HPy
hpy_abi_impl(HPyContext *ctx, HPy self)
{
  return HPyUnicode_FromString(ctx, HPY_ABI);
}

HPyDef_SLOT(module_exec, HPy_mod_exec)
static int
module_exec_impl(HPyContext *ctx, HPy module)
{
  HPy version = HPyUnicode_FromString(ctx, UJSON_VERSION);
  HPy json_decode_error =
      HPyErr_NewException(ctx, "ujson_hpy.JSONDecodeError", ctx->h_ValueError,
                          HPy_NULL);

  if (HPy_IsNull(version) || HPy_IsNull(json_decode_error))
  {
    HPy_Close(ctx, version);
    HPy_Close(ctx, json_decode_error);
    return -1;
  }

  if (HPy_SetAttr_s(ctx, module, "__version__", version) < 0)
  {
    HPy_Close(ctx, version);
    HPy_Close(ctx, json_decode_error);
    return -1;
  }
  HPy_Close(ctx, version);

  if (HPy_SetAttr_s(ctx, module, "JSONDecodeError", json_decode_error) < 0)
  {
    HPy_Close(ctx, json_decode_error);
    return -1;
  }
  HPyGlobal_Store(ctx, &g_json_decode_error, json_decode_error);
  HPy_Close(ctx, json_decode_error);

  {
    HPy builtins_module = HPyImport_ImportModule(ctx, "builtins");
    HPy decimal_module = HPyImport_ImportModule(ctx, "decimal");
    if (!HPy_IsNull(builtins_module))
    {
      HPy bytearray_type = HPy_GetAttr_s(ctx, builtins_module, "bytearray");
      HPy memoryview_type = HPy_GetAttr_s(ctx, builtins_module, "memoryview");
      if (HPy_IsNull(bytearray_type))
      {
        HPyErr_Clear(ctx);
      }
      else
      {
        HPyGlobal_Store(ctx, &g_bytearray_type, bytearray_type);
        HPy_Close(ctx, bytearray_type);
      }
      if (HPy_IsNull(memoryview_type))
      {
        HPyErr_Clear(ctx);
      }
      else
      {
        HPyGlobal_Store(ctx, &g_memoryview_type, memoryview_type);
        HPy_Close(ctx, memoryview_type);
      }
      HPy_Close(ctx, builtins_module);
    }
    else
    {
      HPyErr_Clear(ctx);
    }

    if (!HPy_IsNull(decimal_module))
    {
      HPy decimal_type = HPy_GetAttr_s(ctx, decimal_module, "Decimal");
      if (HPy_IsNull(decimal_type))
      {
        HPyErr_Clear(ctx);
      }
      else
      {
        HPyGlobal_Store(ctx, &g_decimal_type, decimal_type);
        HPy_Close(ctx, decimal_type);
      }
      HPy_Close(ctx, decimal_module);
    }
    else
    {
      HPyErr_Clear(ctx);
    }
  }

  return 0;
}

static HPyDef *module_defines[] = {
  &hpy_encode,
  &hpy_decode,
  &hpy_dumps,
  &hpy_loads,
  &hpy_dump,
  &hpy_load,
  &hpy_abi,
  &module_exec,
  NULL
};

static HPyModuleDef moduledef = {
  .doc = "Experimental HPy bootstrap for UltraJSON",
  .size = 0,
  .defines = module_defines
};

HPy_MODINIT(ujson_hpy, moduledef)
