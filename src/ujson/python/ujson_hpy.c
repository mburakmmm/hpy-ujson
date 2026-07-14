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

typedef struct HpyJsonValue {
  HPy handle;
  struct HpyJsonValue *next;
} HpyJsonValue;

typedef struct HpyJsonValueBlock {
  struct HpyJsonValueBlock *next;
  HpyJsonValue items[256];
} HpyJsonValueBlock;

typedef struct {
  HPyContext *ctx;
  HpyJsonValue *free_values;
  HpyJsonValueBlock *value_blocks;
} HpyDecoderState;

typedef struct {
  HPyContext *ctx;
  HPy default_fn;
  struct HpyEncoderTypeContext *free_type_contexts;
  struct HpyEncoderTypeContextBlock *type_context_blocks;
  union {
    JSINT64 long_value;
    JSUINT64 unsigned_long_value;
    double double_value;
  } scalar;
} HpyEncoderState;

typedef struct HpyEncoderTypeContext {
  JSPFN_ITEREND iterEnd;
  JSPFN_ITERNEXT iterNext;
  JSPFN_ITERGETNAME iterGetName;
  JSPFN_ITERGETVALUE iterGetValue;
  struct HpyEncoderTypeContext *next;
  HPy newObj;
  HPy utf8BytesObj;
  HPy dictObj;
  HPy_ssize_t index;
  HPy_ssize_t size;
  JSOBJ itemValue;
  HPy itemName;
  HPy rawJSONValue;
  bool has_resources;

  union {
    JSINT64 longValue;
    JSUINT64 unsignedLongValue;
  };
} HpyEncoderTypeContext;

typedef struct HpyEncoderTypeContextBlock {
  struct HpyEncoderTypeContextBlock *next;
  HpyEncoderTypeContext items[64];
} HpyEncoderTypeContextBlock;

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

static inline void
hpy_close_if_nonnull(HPyContext *ctx, HPy handle)
{
  if (!HPy_IsNull(handle))
  {
    HPy_Close(ctx, handle);
  }
}

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
  value->next = NULL;
  return value;
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

static int
hpy_decoder_grow_value_pool(HpyDecoderState *state)
{
  HpyJsonValueBlock *block = malloc(sizeof(HpyJsonValueBlock));
  size_t i;

  if (block == NULL)
  {
    HPyErr_NoMemory(state->ctx);
    return -1;
  }

  block->next = state->value_blocks;
  state->value_blocks = block;

  for (i = 0; i < (sizeof(block->items) / sizeof(block->items[0])) - 1; i++)
  {
    block->items[i].next = &block->items[i + 1];
    block->items[i].handle = HPy_NULL;
  }
  block->items[i].next = state->free_values;
  block->items[i].handle = HPy_NULL;
  state->free_values = &block->items[0];
  return 0;
}

static HpyJsonValue *
hpy_decoder_value_new(HpyDecoderState *state, HPy handle)
{
  HpyJsonValue *value;

  if (state->free_values == NULL && hpy_decoder_grow_value_pool(state) < 0)
  {
    HPy_Close(state->ctx, handle);
    return NULL;
  }

  value = state->free_values;
  state->free_values = value->next;
  value->handle = handle;
  value->next = NULL;
  return value;
}

static HPy
hpy_decoder_value_detach(HpyDecoderState *state, HpyJsonValue *value)
{
  HPy handle = value->handle;
  value->handle = HPy_NULL;
  value->next = state->free_values;
  state->free_values = value;
  return handle;
}

static void
hpy_decoder_value_release(HpyDecoderState *state, HpyJsonValue *value)
{
  if (value == NULL)
  {
    return;
  }

  HPy_Close(state->ctx, value->handle);
  value->handle = HPy_NULL;
  value->next = state->free_values;
  state->free_values = value;
}

static void
hpy_decoder_cleanup_value_pool(HpyDecoderState *state)
{
  HpyJsonValueBlock *block = state->value_blocks;

  while (block != NULL)
  {
    HpyJsonValueBlock *next = block->next;
    free(block);
    block = next;
  }

  state->value_blocks = NULL;
  state->free_values = NULL;
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
  HpyJsonValue *value = hpy_json_value_new(ctx, HPy_Dup(ctx, handle));
  if (value == NULL)
  {
    return NULL;
  }
  return value;
}

static void
hpy_json_value_release_jsobj(HPyContext *ctx, JSOBJ obj)
{
  hpy_json_value_release(ctx, hpy_json_value_from_jsobj(obj));
}

#ifndef HPY_ABI_CPYTHON
static int
hpy_json_value_replace_jsobj(HPyContext *ctx, JSOBJ *obj, HPy handle)
{
  HpyJsonValue *value;

  if (*obj == NULL)
  {
    value = hpy_json_value_new(ctx, handle);
    if (value == NULL)
    {
      return -1;
    }
    *obj = value;
    return 0;
  }

  value = hpy_json_value_from_jsobj(*obj);
  HPy_Close(ctx, value->handle);
  value->handle = handle;
  return 0;
}
#endif

#ifdef HPY_ABI_CPYTHON
static int
hpy_json_value_replace_borrowed(HPyContext *ctx, JSOBJ *obj, HPy handle)
{
  HpyJsonValue *value;

  if (*obj == NULL)
  {
    value = malloc(sizeof(*value));
    if (value == NULL)
    {
      HPyErr_NoMemory(ctx);
      return -1;
    }
    value->next = NULL;
    *obj = value;
  }

  value = hpy_json_value_from_jsobj(*obj);
  value->handle = handle;
  return 0;
}

static void
hpy_json_value_release_borrowed(JSOBJ obj)
{
  free(hpy_json_value_from_jsobj(obj));
}
#endif

static JSOBJ
Decoder_newString(void *prv, JSUINT32 *start, JSUINT32 *end)
{
  HpyDecoderState *state = prv;
  HPy handle;

#ifdef HPY_ABI_CPYTHON
  handle = _py2h(PyUnicode_FromKindAndData(
      PyUnicode_4BYTE_KIND, (const Py_UCS4 *) start, (Py_ssize_t) (end - start)));
#else
  handle = HPyUnicode_FromWideChar(
      state->ctx, (const wchar_t *) start, (HPy_ssize_t) (end - start));
#endif
  if (HPy_IsNull(handle))
  {
    return NULL;
  }

  return hpy_decoder_value_new(state, handle);
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

  hpy_decoder_value_release(state, hpy_json_value_from_jsobj(name));
  hpy_decoder_value_release(state, hpy_json_value_from_jsobj(value));
}

static void
Decoder_arrayAddItem(void *prv, JSOBJ obj, JSOBJ value)
{
  HpyDecoderState *state = prv;
  HPyContext *ctx = state->ctx;
  HPyList_Append(ctx, hpy_json_handle_from_jsobj(obj),
                 hpy_json_handle_from_jsobj(value));
  hpy_decoder_value_release(state, hpy_json_value_from_jsobj(value));
}

static JSOBJ
Decoder_newTrue(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_decoder_value_new(state,
                               HPy_Dup(state->ctx, state->ctx->h_True));
}

static JSOBJ
Decoder_newFalse(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_decoder_value_new(state,
                               HPy_Dup(state->ctx, state->ctx->h_False));
}

static JSOBJ
Decoder_newNull(void *prv)
{
  HpyDecoderState *state = prv;
  return hpy_decoder_value_new(state,
                               HPy_Dup(state->ctx, state->ctx->h_None));
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
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
  return hpy_decoder_value_new(state, handle);
}

static JSOBJ
Decoder_newIntegerFromString(void *prv, char *value, size_t length)
{
  HpyDecoderState *state = prv;
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

#ifdef HPY_ABI_CPYTHON
  result = _py2h(PyLong_FromString(buf, NULL, 10));
  free(buf);
  if (HPy_IsNull(result))
  {
    return NULL;
  }
#else
  {
    HPy arg = HPyUnicode_FromString(state->ctx, buf);
    free(buf);
    if (HPy_IsNull(arg))
    {
      return NULL;
    }

    {
      HPy call_args[] = {arg};
      result = HPy_Call(state->ctx, state->ctx->h_LongType, call_args, 1,
                        HPy_NULL);
    }
    HPy_Close(state->ctx, arg);
    if (HPy_IsNull(result))
    {
      return NULL;
    }
  }
#endif

  wrapped = result;
  return hpy_decoder_value_new(state, wrapped);
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
  return hpy_decoder_value_new(state, handle);
}

static void
Decoder_releaseObject(void *prv, JSOBJ obj)
{
  HpyDecoderState *state = prv;
  hpy_decoder_value_release(state, hpy_json_value_from_jsobj(obj));
}

static const char *loads_kwlist[] = {"obj", NULL};
static const char *load_kwlist[] = {"fp", NULL};

static HPy
hpy_loads_decode_raw(HPyContext *ctx, const char *raw, size_t raw_len)
{
  HpyDecoderState state = {.ctx = ctx, .free_values = NULL, .value_blocks = NULL};
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
      hpy_decoder_value_release(&state, ret);
    }
    hpy_decoder_cleanup_value_pool(&state);
    return HPy_NULL;
  }

  if (decoder.errorStr != NULL)
  {
    set_decode_error(ctx, decoder.errorStr);
    if (ret != NULL)
    {
      hpy_decoder_value_release(&state, ret);
    }
    hpy_decoder_cleanup_value_pool(&state);
    return HPy_NULL;
  }

  if (ret == NULL)
  {
    HPyErr_SetString(ctx, ctx->h_RuntimeError, "JSON decoder returned NULL");
    hpy_decoder_cleanup_value_pool(&state);
    return HPy_NULL;
  }

  {
    HPy result = hpy_decoder_value_detach(&state, ret);
    hpy_decoder_cleanup_value_pool(&state);
    return result;
  }
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
  HPy view = HPy_NULL;
  HPy contiguous = HPy_NULL;
  HPy tobytes = HPy_NULL;
  HPy bytes_arg = HPy_NULL;
  int is_contiguous;

  if (HPy_IsNull(memoryview_type))
  {
    return HPy_NULL;
  }

  {
    HPy call_args[] = {arg};
    view = HPy_Call(ctx, memoryview_type, call_args, 1, HPy_NULL);
  }
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

  tobytes = HPy_GetAttr_s(ctx, view, "tobytes");
  if (HPy_IsNull(tobytes))
  {
    HPy_Close(ctx, view);
    return HPy_NULL;
  }

  bytes_arg = HPy_Call(ctx, tobytes, NULL, 0, HPy_NULL);
  HPy_Close(ctx, tobytes);
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

#ifdef HPY_ABI_CPYTHON
static HPy
hpy_loads_decode_buffer_like_cpython(HPyContext *ctx, HPy arg)
{
  Py_buffer buffer;
  int status = PyObject_GetBuffer(_h2py(arg), &buffer, PyBUF_C_CONTIGUOUS);
  if (status < 0)
  {
    HPyErr_Clear(ctx);
    return HPy_NULL;
  }

  {
    HPy result = hpy_loads_decode_raw(ctx, (const char *) buffer.buf,
                                      (size_t) buffer.len);
    PyBuffer_Release(&buffer);
    return result;
  }
}
#endif

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

#ifdef HPY_ABI_CPYTHON
  {
    HPy result = hpy_loads_decode_buffer_like_cpython(ctx, arg);
    if (!HPy_IsNull(result))
    {
      return result;
    }
    if (HPyErr_Occurred(ctx))
    {
      return HPy_NULL;
    }
  }
#endif

  {
    HPy bytearray_type = HPyGlobal_Load(ctx, g_bytearray_type);
    int is_bytearray = 0;

    if (!HPy_IsNull(bytearray_type))
    {
      is_bytearray = HPy_TypeCheck(ctx, arg, bytearray_type);
      HPy_Close(ctx, bytearray_type);
      if (is_bytearray)
      {
        HPy bytes_arg;
        HPy call_args[] = {arg};
        bytes_arg = HPy_Call(ctx, ctx->h_BytesType, call_args, 1, HPy_NULL);
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
                   "Expected str, bytes, bytearray, or a C-contiguous "
                   "bytes-like object");
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

static int
hpy_encoder_grow_type_context_pool(HpyEncoderState *state)
{
  HpyEncoderTypeContextBlock *block = malloc(sizeof(HpyEncoderTypeContextBlock));
  size_t i;

  if (block == NULL)
  {
    HPyErr_NoMemory(state->ctx);
    return -1;
  }

  block->next = state->type_context_blocks;
  state->type_context_blocks = block;

  for (i = 0; i < (sizeof(block->items) / sizeof(block->items[0])) - 1; i++)
  {
    block->items[i].next = &block->items[i + 1];
  }
  block->items[i].next = state->free_type_contexts;
  state->free_type_contexts = &block->items[0];
  return 0;
}

static HpyEncoderTypeContext *
hpy_encoder_alloc_type_context(HpyEncoderState *state)
{
  HpyEncoderTypeContext *etc;

  if (state->free_type_contexts == NULL &&
      hpy_encoder_grow_type_context_pool(state) < 0)
  {
    return NULL;
  }

  etc = state->free_type_contexts;
  state->free_type_contexts = etc->next;
  memset(etc, 0, sizeof(*etc));
  etc->newObj = HPy_NULL;
  etc->utf8BytesObj = HPy_NULL;
  etc->dictObj = HPy_NULL;
  etc->itemName = HPy_NULL;
  etc->rawJSONValue = HPy_NULL;
  etc->next = NULL;
  return etc;
}

static void
hpy_encoder_release_type_context(HpyEncoderState *state,
                                 HpyEncoderTypeContext *etc)
{
  if (etc == NULL)
  {
    return;
  }

  if (etc->has_resources && etc->itemValue != NULL)
  {
#ifdef HPY_ABI_CPYTHON
    hpy_json_value_release_borrowed(etc->itemValue);
#else
    hpy_json_value_release_jsobj(state->ctx, etc->itemValue);
#endif
    etc->itemValue = NULL;
  }
  if (etc->has_resources)
  {
    hpy_close_if_nonnull(state->ctx, etc->itemName);
    hpy_close_if_nonnull(state->ctx, etc->dictObj);
    hpy_close_if_nonnull(state->ctx, etc->utf8BytesObj);
    hpy_close_if_nonnull(state->ctx, etc->rawJSONValue);
    hpy_close_if_nonnull(state->ctx, etc->newObj);
  }
  etc->next = state->free_type_contexts;
  state->free_type_contexts = etc;
}

static void
hpy_encoder_cleanup_type_context_pool(HpyEncoderState *state)
{
  HpyEncoderTypeContextBlock *block = state->type_context_blocks;

  while (block != NULL)
  {
    HpyEncoderTypeContextBlock *next = block->next;
    free(block);
    block = next;
  }

  state->type_context_blocks = NULL;
  state->free_type_contexts = NULL;
}

static HPy
hpy_encoder_get_obj_handle(JSOBJ obj, JSONTypeContext *tc)
{
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  if (etc != NULL && !HPy_IsNull(etc->newObj))
  {
    return etc->newObj;
  }
  return hpy_json_handle_from_jsobj(obj);
}

static int
hpy_maybe_get_attr_s(HPyContext *ctx, HPy obj, const char *name, HPy *out)
{
  *out = HPy_GetAttr_s(ctx, obj, name);
  if (!HPy_IsNull(*out))
  {
    return 1;
  }

  if (!HPyErr_ExceptionMatches(ctx, ctx->h_AttributeError))
  {
    return -1;
  }
  HPyErr_Clear(ctx);
  return 0;
}

static void
hpy_encoder_type_context_cleanup_for_state(HpyEncoderState *state,
                                           HpyEncoderTypeContext *etc)
{
  if (etc == NULL)
  {
    return;
  }
  hpy_encoder_release_type_context(state, etc);
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

static HPy
hpy_unicode_to_utf8_bytes(HPyContext *ctx, HPy obj)
{
  HPy result = HPyUnicode_AsUTF8String(ctx, obj);
  if (!HPy_IsNull(result))
  {
    return result;
  }

  if (!HPyErr_ExceptionMatches(ctx, ctx->h_UnicodeEncodeError))
  {
    return HPy_NULL;
  }
  HPyErr_Clear(ctx);
  return hpy_unicode_encode_surrogatepass(ctx, obj);
}

static const char *
hpy_unicode_to_utf8_raw(HPyContext *ctx, HPy obj, size_t *out_len, HPy *bytes_holder)
{
  HPy_ssize_t len = 0;
  const char *raw;

  hpy_close_if_nonnull(ctx, *bytes_holder);
  *bytes_holder = HPy_NULL;

  raw = HPyUnicode_AsUTF8AndSize(ctx, obj, &len);
  if (raw != NULL)
  {
    *bytes_holder = HPy_Dup(ctx, obj);
    if (HPy_IsNull(*bytes_holder))
    {
      return NULL;
    }
    *out_len = (size_t) len;
    return raw;
  }

  if (!HPyErr_ExceptionMatches(ctx, ctx->h_UnicodeEncodeError))
  {
    return NULL;
  }
  HPyErr_Clear(ctx);

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
  HpyEncoderState *state = hpy_encoder_state(tc);
  HPyContext *ctx = state->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy value = hpy_encoder_get_obj_handle(obj, tc);
  (void) out_value;

  if (etc == NULL)
  {
    HPy_ssize_t len = 0;
    const char *raw = HPyUnicode_AsUTF8AndSize(ctx, value, &len);
    if (raw != NULL)
    {
      *out_len = (size_t) len;
      return raw;
    }
    if (!HPyErr_ExceptionMatches(ctx, ctx->h_UnicodeEncodeError))
    {
      return NULL;
    }
    HPyErr_Clear(ctx);

    etc = hpy_encoder_alloc_type_context(state);
    if (etc == NULL)
    {
      return NULL;
    }
    tc->prv = etc;
  }

  {
    const char *raw = hpy_unicode_to_utf8_raw(
        ctx, value, out_len, &etc->utf8BytesObj);
    if (!HPy_IsNull(etc->utf8BytesObj))
    {
      etc->has_resources = true;
    }
    return raw;
  }
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
  if (tc->prv == NULL)
  {
    *((JSINT64 *) out_value) = hpy_encoder_state(tc)->scalar.long_value;
  }
  else
  {
    *((JSINT64 *) out_value) = hpy_encoder_type_context(tc)->longValue;
  }
  return NULL;
}

static void *
hpy_long_to_uint64(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  (void) obj;
  (void) out_len;
  if (tc->prv == NULL)
  {
    *((JSUINT64 *) out_value) =
        hpy_encoder_state(tc)->scalar.unsigned_long_value;
  }
  else
  {
    *((JSUINT64 *) out_value) =
        hpy_encoder_type_context(tc)->unsignedLongValue;
  }
  return NULL;
}

static void *
hpy_float_to_double(JSOBJ obj, JSONTypeContext *tc, void *out_value, size_t *out_len)
{
  (void) out_len;
  if (tc->prv == NULL)
  {
    *((double *) out_value) = hpy_encoder_state(tc)->scalar.double_value;
  }
  else
  {
    HPyContext *ctx = hpy_encoder_state(tc)->ctx;
    *((double *) out_value) =
        HPyFloat_AsDouble(ctx, hpy_encoder_get_obj_handle(obj, tc));
  }
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

static int
hpy_dict_prepare_key(HPyContext *ctx, HPy key, HpyEncoderTypeContext *etc)
{
  hpy_close_if_nonnull(ctx, etc->itemName);
  etc->itemName = HPy_NULL;

  if (HPyUnicode_Check(ctx, key))
  {
    etc->itemName = hpy_unicode_to_utf8_bytes(ctx, key);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
  }

  if (HPyBytes_Check(ctx, key))
  {
    etc->itemName = HPy_Dup(ctx, key);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
  }

  if (HPy_Is(ctx, key, ctx->h_True))
  {
    etc->itemName = HPyBytes_FromStringAndSize(ctx, "true", 4);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
  }

  if (HPy_Is(ctx, key, ctx->h_False))
  {
    etc->itemName = HPyBytes_FromStringAndSize(ctx, "false", 5);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
  }

  if (HPy_Is(ctx, key, ctx->h_None))
  {
    etc->itemName = HPyBytes_FromStringAndSize(ctx, "null", 4);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
  }

  {
    HPy key_str = HPy_Str(ctx, key);
    if (HPy_IsNull(key_str))
    {
      return -1;
    }
    etc->itemName = hpy_unicode_to_utf8_bytes(ctx, key_str);
    HPy_Close(ctx, key_str);
    return HPy_IsNull(etc->itemName) ? -1 : 0;
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

#ifdef HPY_ABI_CPYTHON
  PyObject *py_item = PyTuple_GET_ITEM(_h2py(tuple), etc->index);
  item = _py2h(py_item);
#else
  item = HPy_GetItem_i(ctx, tuple, etc->index);
  if (HPy_IsNull(item))
  {
    return -1;
  }
#endif

#ifdef HPY_ABI_CPYTHON
  if (hpy_json_value_replace_borrowed(ctx, &etc->itemValue, item) < 0)
#else
  if (hpy_json_value_replace_jsobj(ctx, &etc->itemValue, item) < 0)
#endif
  {
    return -1;
  }
  etc->has_resources = true;

  etc->index += 1;
  return 1;
}

static void
hpy_tuple_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;
  if (etc->itemValue != NULL)
  {
#ifdef HPY_ABI_CPYTHON
    hpy_json_value_release_borrowed(etc->itemValue);
#else
    HPyContext *ctx = hpy_encoder_state(tc)->ctx;
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
#endif
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
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  HPy list = hpy_encoder_get_obj_handle(obj, tc);
  HPy item;

  if (etc->index >= etc->size)
  {
    return 0;
  }

#ifdef HPY_ABI_CPYTHON
  PyObject *py_item = PyList_GET_ITEM(_h2py(list), etc->index);
  item = _py2h(py_item);
#else
  item = HPy_GetItem_i(ctx, list, etc->index);
  if (HPy_IsNull(item))
  {
    return -1;
  }
#endif

#ifdef HPY_ABI_CPYTHON
  if (hpy_json_value_replace_borrowed(ctx, &etc->itemValue, item) < 0)
#else
  if (hpy_json_value_replace_jsobj(ctx, &etc->itemValue, item) < 0)
#endif
  {
    return -1;
  }
  etc->has_resources = true;

  etc->index += 1;
  return 1;
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
  HPy key = HPy_NULL;
  HPy value = HPy_NULL;
  int result = 0;

  (void) obj;

  if (etc->index >= etc->size)
  {
    return 0;
  }

#ifdef HPY_ABI_CPYTHON
  {
    PyObject *py_key = PyList_GET_ITEM(_h2py(etc->newObj), etc->index);
    PyObject *py_value = PyDict_GetItem(_h2py(dict), py_key);
    if (py_value == NULL)
    {
      return -1;
    }
    key = _py2h(py_key);
    value = _py2h(py_value);
  }
#else
  key = HPy_GetItem_i(ctx, etc->newObj, etc->index);
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
#endif

  if (hpy_dict_prepare_key(ctx, key, etc) < 0)
  {
#ifndef HPY_ABI_CPYTHON
    HPy_Close(ctx, value);
    HPy_Close(ctx, key);
#endif
    return -1;
  }

#ifdef HPY_ABI_CPYTHON
  if (hpy_json_value_replace_borrowed(ctx, &etc->itemValue, value) < 0)
#else
  if (hpy_json_value_replace_jsobj(ctx, &etc->itemValue, value) < 0)
#endif
  {
#ifndef HPY_ABI_CPYTHON
    HPy_Close(ctx, key);
#endif
    return -1;
  }
  etc->has_resources = true;

#ifndef HPY_ABI_CPYTHON
  HPy_Close(ctx, key);
#endif
  etc->index += 1;
  result = 1;
  return result;
}

#ifdef HPY_ABI_CPYTHON
static int
hpy_dict_iter_next_cpython_unsorted(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  PyObject *py_key = NULL;
  PyObject *py_value = NULL;
  HPy value;

  (void) obj;

  if (!PyDict_Next(_h2py(etc->dictObj), (Py_ssize_t *) &etc->index, &py_key, &py_value))
  {
    return 0;
  }

  if (hpy_dict_prepare_key(ctx, _py2h(py_key), etc) < 0)
  {
    return -1;
  }

  value = _py2h(py_value);
  if (hpy_json_value_replace_borrowed(ctx, &etc->itemValue, value) < 0)
  {
    return -1;
  }
  etc->has_resources = true;

  return 1;
}
#endif

static void
hpy_dict_iter_end(JSOBJ obj, JSONTypeContext *tc)
{
  HPyContext *ctx = hpy_encoder_state(tc)->ctx;
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;

  if (etc->itemValue != NULL)
  {
#ifdef HPY_ABI_CPYTHON
    hpy_json_value_release_borrowed(etc->itemValue);
#else
    hpy_json_value_release_jsobj(ctx, etc->itemValue);
#endif
    etc->itemValue = NULL;
  }
  hpy_close_if_nonnull(ctx, etc->itemName);
  etc->itemName = HPy_NULL;
  hpy_close_if_nonnull(ctx, etc->dictObj);
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
  (void) obj;

  {
    HPy_ssize_t len = HPyBytes_Size(ctx, etc->itemName);
    if (len < 0)
    {
      return NULL;
    }
    *out_len = (size_t) len;
    return (char *) HPyBytes_AsString(ctx, etc->itemName);
  }
}

static int
hpy_setup_dict_iter(HPyContext *ctx, HPy dict_obj, HpyEncoderTypeContext *etc,
                    JSONObjectEncoder *enc)
{
  HPy sort_method = HPy_NULL;
  HPy sort_result = HPy_NULL;

  etc->dictObj = HPy_Dup(ctx, dict_obj);
  if (HPy_IsNull(etc->dictObj))
  {
    return -1;
  }

#ifdef HPY_ABI_CPYTHON
  if (!enc->sortKeys)
  {
    etc->iterNext = hpy_dict_iter_next_cpython_unsorted;
    etc->iterEnd = hpy_dict_iter_end;
    etc->iterGetValue = hpy_dict_iter_get_value;
    etc->iterGetName = hpy_dict_iter_get_name;
    etc->index = 0;
    etc->size = 0;
    return 0;
  }
#endif

  etc->newObj = HPyDict_Keys(ctx, dict_obj);
  if (HPy_IsNull(etc->newObj))
  {
    HPy_Close(ctx, etc->dictObj);
    etc->dictObj = HPy_NULL;
    return -1;
  }

  if (enc->sortKeys)
  {
    sort_method = HPy_GetAttr_s(ctx, etc->newObj, "sort");
    if (HPy_IsNull(sort_method))
    {
      HPy_Close(ctx, etc->newObj);
      HPy_Close(ctx, etc->dictObj);
      etc->newObj = HPy_NULL;
      etc->dictObj = HPy_NULL;
      return -1;
    }

    sort_result = HPy_Call(ctx, sort_method, NULL, 0, HPy_NULL);
    HPy_Close(ctx, sort_method);
    if (HPy_IsNull(sort_result))
    {
      HPy_Close(ctx, etc->newObj);
      HPy_Close(ctx, etc->dictObj);
      etc->newObj = HPy_NULL;
      etc->dictObj = HPy_NULL;
      return -1;
    }
    HPy_Close(ctx, sort_result);
  }

  etc->size = HPy_Length(ctx, etc->newObj);
  if (etc->size < 0)
  {
    HPy_Close(ctx, etc->newObj);
    HPy_Close(ctx, etc->dictObj);
    etc->newObj = HPy_NULL;
    etc->dictObj = HPy_NULL;
    return -1;
  }

  etc->iterNext = hpy_dict_iter_next;
  etc->iterEnd = hpy_dict_iter_end;
  etc->iterGetValue = hpy_dict_iter_get_value;
  etc->iterGetName = hpy_dict_iter_get_name;
  etc->index = 0;
  return 0;
}

enum HpyScalarClassification {
  HPY_SCALAR_ERROR = -1,
  HPY_NOT_SCALAR = 0,
  HPY_SCALAR_HANDLED = 1,
  HPY_SCALAR_NEEDS_CONTEXT = 2,
};

static int
hpy_encoder_classify_scalar(HpyEncoderState *state, HPy value,
                            JSONTypeContext *tc, JSONObjectEncoder *enc)
{
  HPyContext *ctx = state->ctx;

  if (HPy_Is(ctx, value, ctx->h_True))
  {
    tc->type = JT_TRUE;
    return HPY_SCALAR_HANDLED;
  }

  if (HPy_Is(ctx, value, ctx->h_False))
  {
    tc->type = JT_FALSE;
    return HPY_SCALAR_HANDLED;
  }

  if (HPy_TypeCheck(ctx, value, ctx->h_LongType))
  {
    state->scalar.long_value = (JSINT64) HPyLong_AsLongLong(ctx, value);
    if (!(state->scalar.long_value == -1 && HPyErr_Occurred(ctx)))
    {
      tc->type = JT_LONG;
      return HPY_SCALAR_HANDLED;
    }
    if (!HPyErr_ExceptionMatches(ctx, ctx->h_OverflowError))
    {
      return HPY_SCALAR_ERROR;
    }
    HPyErr_Clear(ctx);

    state->scalar.unsigned_long_value =
        (JSUINT64) HPyLong_AsUnsignedLongLong(ctx, value);
    if (!(state->scalar.unsigned_long_value == (JSUINT64) -1 &&
          HPyErr_Occurred(ctx)))
    {
      tc->type = JT_ULONG;
      return HPY_SCALAR_HANDLED;
    }
    if (!HPyErr_ExceptionMatches(ctx, ctx->h_OverflowError))
    {
      return HPY_SCALAR_ERROR;
    }
    HPyErr_Clear(ctx);
    return HPY_SCALAR_NEEDS_CONTEXT;
  }

  if (HPyBytes_Check(ctx, value))
  {
    if (enc->rejectBytes)
    {
      HPyErr_SetString(ctx, ctx->h_TypeError,
                       "reject_bytes is on and bytes is bytes");
      return HPY_SCALAR_ERROR;
    }
    tc->type = JT_UTF8;
    return HPY_SCALAR_HANDLED;
  }

  if (HPyUnicode_Check(ctx, value))
  {
    tc->type = JT_UTF8;
    return HPY_SCALAR_HANDLED;
  }

  if (HPy_Is(ctx, value, ctx->h_None))
  {
    tc->type = JT_NULL;
    return HPY_SCALAR_HANDLED;
  }

  if (hpy_object_is_float_type(ctx, value) ||
      hpy_object_is_decimal_type(ctx, value))
  {
    state->scalar.double_value = HPyFloat_AsDouble(ctx, value);
    if (state->scalar.double_value == -1.0 && HPyErr_Occurred(ctx))
    {
      return HPY_SCALAR_ERROR;
    }
    tc->type = JT_DOUBLE;
    return HPY_SCALAR_HANDLED;
  }

  return HPY_NOT_SCALAR;
}

static void
hpy_encoder_begin_type_context(JSOBJ obj, JSONTypeContext *tc, JSONObjectEncoder *enc)
{
  HpyEncoderState *state = (HpyEncoderState *) tc->encoder_prv;
  HPyContext *ctx = state->ctx;
  HpyEncoderTypeContext *etc;
  HPy method = HPy_NULL;
  HPy value = HPy_NULL;
  int scalar_result;
  int level = 0;

  tc->prv = NULL;

  if (obj == NULL)
  {
    tc->type = JT_INVALID;
    return;
  }

  value = hpy_json_handle_from_jsobj(obj);
  scalar_result = hpy_encoder_classify_scalar(state, value, tc, enc);
  if (scalar_result == HPY_SCALAR_HANDLED)
  {
    return;
  }
  if (scalar_result == HPY_SCALAR_ERROR)
  {
    tc->type = JT_INVALID;
    return;
  }

  etc = hpy_encoder_alloc_type_context(state);
  if (etc == NULL)
  {
    tc->type = JT_INVALID;
    return;
  }
  tc->prv = etc;

  if (scalar_result == HPY_NOT_SCALAR)
  {
    goto COMPLEX;
  }

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
    etc->has_resources = true;
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

COMPLEX:
  if (HPyDict_Check(ctx, value))
  {
    if (hpy_setup_dict_iter(ctx, value, etc, enc) < 0)
    {
      goto INVALID;
    }
    etc->has_resources = true;
    tc->type = JT_OBJECT;
    return;
  }

  if (HPyList_Check(ctx, value))
  {
    etc->iterEnd = hpy_list_iter_end;
    etc->iterNext = hpy_list_iter_next;
    etc->iterGetValue = hpy_list_iter_get_value;
    etc->index = 0;
#ifdef HPY_ABI_CPYTHON
    etc->size = PyList_GET_SIZE(_h2py(value));
#else
    etc->size = HPy_Length(ctx, value);
    if (etc->size < 0)
    {
      goto INVALID;
    }
#endif
    tc->type = JT_ARRAY;
    return;
  }

  if (HPyTuple_Check(ctx, value))
  {
    etc->iterEnd = hpy_tuple_iter_end;
    etc->iterNext = hpy_tuple_iter_next;
    etc->iterGetValue = hpy_tuple_iter_get_value;
    etc->index = 0;
#ifdef HPY_ABI_CPYTHON
    etc->size = PyTuple_GET_SIZE(_h2py(value));
#else
    etc->size = HPy_Length(ctx, value);
    if (etc->size < 0)
    {
      goto INVALID;
    }
#endif
    tc->type = JT_ARRAY;
    return;
  }

  {
    int has_to_dict = hpy_maybe_get_attr_s(ctx, value, "toDict", &method);
    if (has_to_dict < 0)
    {
      goto INVALID;
    }
    if (has_to_dict)
    {
      HPy to_dict_result;
      to_dict_result = HPy_Call(ctx, method, NULL, 0, HPy_NULL);
      HPy_Close(ctx, method);
      method = HPy_NULL;
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
      etc->has_resources = true;
      HPy_Close(ctx, to_dict_result);
      tc->type = JT_OBJECT;
      return;
    }
  }

  {
    int has_json = hpy_maybe_get_attr_s(ctx, value, "__json__", &method);
    if (has_json < 0)
    {
      goto INVALID;
    }
    if (has_json)
    {
      etc->rawJSONValue = HPy_Call(ctx, method, NULL, 0, HPy_NULL);
      HPy_Close(ctx, method);
      method = HPy_NULL;
      if (HPy_IsNull(etc->rawJSONValue))
      {
        goto INVALID;
      }
      etc->has_resources = true;
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
      HPy args[] = {value};
      new_obj = HPy_Call(ctx, state->default_fn, args, 1, HPy_NULL);
    }
    if (HPy_IsNull(new_obj))
    {
      goto INVALID;
    }

    HPy_Close(ctx, etc->newObj);
    etc->newObj = new_obj;
    etc->has_resources = true;
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
  HPy_Close(ctx, method);
  tc->type = JT_INVALID;
  hpy_encoder_type_context_cleanup_for_state(state, etc);
  tc->prv = NULL;
}

static void
hpy_encoder_end_type_context(JSOBJ obj, JSONTypeContext *tc)
{
  HpyEncoderTypeContext *etc = hpy_encoder_type_context(tc);
  (void) obj;
  if (etc == NULL)
  {
    return;
  }
  hpy_encoder_type_context_cleanup_for_state(hpy_encoder_state(tc), etc);
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
    .free_type_contexts = NULL,
    .type_context_blocks = NULL,
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
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }

    if (HPy_Length(ctx, separators) != 2)
    {
      HPyErr_SetString(ctx, ctx->h_ValueError,
                       "expected tuple of size 2 as separator");
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }

    item_sep = HPy_GetItem_i(ctx, separators, 0);
    if (HPy_IsNull(item_sep))
    {
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }
    key_sep = HPy_GetItem_i(ctx, separators, 1);
    if (HPy_IsNull(key_sep))
    {
      HPy_Close(ctx, item_sep);
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }

    if (!HPyUnicode_Check(ctx, item_sep))
    {
      HPy_Close(ctx, item_sep);
      HPy_Close(ctx, key_sep);
      HPyErr_SetString(ctx, ctx->h_TypeError, "expected str as item separator");
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }
    if (!HPyUnicode_Check(ctx, key_sep))
    {
      HPy_Close(ctx, item_sep);
      HPy_Close(ctx, key_sep);
      HPyErr_SetString(ctx, ctx->h_TypeError, "expected str as key separator");
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }

    item_sep_bytes = hpy_unicode_to_utf8_bytes(ctx, item_sep);
    HPy_Close(ctx, item_sep);
    if (HPy_IsNull(item_sep_bytes))
    {
      HPy_Close(ctx, key_sep);
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }
    {
      HPy_ssize_t item_sep_len = HPyBytes_Size(ctx, item_sep_bytes);
      encoder.itemSeparatorChars = HPyBytes_AsString(ctx, item_sep_bytes);
      if (item_sep_len < 0 || encoder.itemSeparatorChars == NULL)
      {
        HPy_Close(ctx, key_sep);
        HPy_Close(ctx, item_sep_bytes);
        hpy_encoder_cleanup_type_context_pool(&state);
        return HPy_NULL;
      }
      encoder.itemSeparatorLength = (size_t) item_sep_len;
    }

    key_sep_bytes = hpy_unicode_to_utf8_bytes(ctx, key_sep);
    HPy_Close(ctx, key_sep);
    if (HPy_IsNull(key_sep_bytes))
    {
      HPy_Close(ctx, item_sep_bytes);
      hpy_encoder_cleanup_type_context_pool(&state);
      return HPy_NULL;
    }
    {
      HPy_ssize_t key_sep_len = HPyBytes_Size(ctx, key_sep_bytes);
      encoder.keySeparatorChars = HPyBytes_AsString(ctx, key_sep_bytes);
      if (key_sep_len < 0 || encoder.keySeparatorChars == NULL)
      {
        HPy_Close(ctx, item_sep_bytes);
        HPy_Close(ctx, key_sep_bytes);
        hpy_encoder_cleanup_type_context_pool(&state);
        return HPy_NULL;
      }
      encoder.keySeparatorLength = (size_t) key_sep_len;
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
    hpy_encoder_cleanup_type_context_pool(&state);
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
    hpy_encoder_cleanup_type_context_pool(&state);
    return HPy_NULL;
  }

  if (HPyErr_Occurred(ctx))
  {
    if (ret != NULL && ret != buffer)
    {
      encoder.free(ret);
    }
    hpy_encoder_cleanup_type_context_pool(&state);
    return HPy_NULL;
  }

  encoded_result = HPyBytes_FromStringAndSize(ctx, ret, (HPy_ssize_t) ret_len);
  if (ret != buffer)
  {
    encoder.free(ret);
  }
  if (HPy_IsNull(encoded_result))
  {
    hpy_encoder_cleanup_type_context_pool(&state);
    return HPy_NULL;
  }
  result = HPyUnicode_FromEncodedObject(ctx, encoded_result, "utf-8",
                                        "surrogatepass");
  HPy_Close(ctx, encoded_result);
  hpy_encoder_cleanup_type_context_pool(&state);
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
  .doc = "HPy port of the UltraJSON encoder and decoder",
  .size = 0,
  .defines = module_defines
};

HPy_MODINIT(ujson_hpy, moduledef)
