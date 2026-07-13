import json
import math
import re
import array
import enum
import io
import sys
from collections import OrderedDict
from pathlib import Path

import pytest


ujson_hpy = pytest.importorskip("ujson_hpy")


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        (True, "true"),
        (False, "false"),
        (None, "null"),
        ([True, False, None], "[true,false,null]"),
        ((True, False, None), "[true,false,null]"),
    ],
)
def test_upstream_subset_dumps_basic_values(value, expected):
    assert ujson_hpy.dumps(value) == expected


def test_upstream_subset_encode_string_conversion():
    test_input = "A string \\ / \b \f \n \r \t </script> &"
    not_html_encoded = '"A string \\\\ \\/ \\b \\f \\n \\r \\t <\\/script> &"'
    html_encoded = (
        '"A string \\\\ \\/ \\b \\f \\n \\r \\t \\u003c\\/script\\u003e \\u0026"'
    )
    not_slashes_escaped = '"A string \\\\ / \\b \\f \\n \\r \\t </script> &"'

    def helper(expected_output, **encode_kwargs):
        output = ujson_hpy.encode(test_input, **encode_kwargs)
        assert output == expected_output
        if encode_kwargs.get("escape_forward_slashes", True):
            assert test_input == json.loads(output)
            assert test_input == ujson_hpy.decode(output)

    helper(not_html_encoded, ensure_ascii=True)
    helper(not_html_encoded, ensure_ascii=False)
    helper(not_html_encoded, ensure_ascii=True, encode_html_chars=False)
    helper(not_html_encoded, ensure_ascii=False, encode_html_chars=False)
    helper(html_encoded, ensure_ascii=True, encode_html_chars=True)
    helper(html_encoded, ensure_ascii=False, encode_html_chars=True)
    helper(not_slashes_escaped, escape_forward_slashes=False)


@pytest.mark.parametrize(
    ("char", "escape"),
    [
        ("<", "\\u003c"),
        (">", "\\u003e"),
        ("&", "\\u0026"),
    ],
)
def test_upstream_subset_html_chars_encoded_individually(char, escape):
    encoded = ujson_hpy.dumps(char, encode_html_chars=True)
    assert escape in encoded.lower()
    assert ujson_hpy.loads(encoded) == char


class SomeObject:
    def __init__(self, message):
        self._message = message

    def __repr__(self):
        return self._message


@pytest.mark.parametrize(
    ("value", "message"),
    [
        (set(), "set() is not JSON serializable"),
        ({1, 2, 3}, "{1, 2, 3} is not JSON serializable"),
        (SomeObject("Some Object"), "Some Object is not JSON serializable"),
    ],
)
def test_upstream_subset_dumps_raises_for_unsupported_values(value, message):
    with pytest.raises(TypeError, match=re.escape(message)):
        ujson_hpy.dumps(value)


@pytest.mark.parametrize(
    "value",
    [
        float("nan"),
        float("inf"),
        -float("inf"),
        [float("nan")],
        {"k": float("inf")},
        [[float("nan")]],
        {"a": {"b": float("inf")}},
    ],
)
def test_upstream_subset_allow_nan_false_raises(value):
    with pytest.raises(OverflowError):
        ujson_hpy.dumps(value, allow_nan=False)


def test_upstream_subset_nan_and_infinity_decode_support():
    text = '["a", NaN, "NaN", Infinity, "Infinity", -Infinity, "-Infinity"]'
    data = ujson_hpy.loads(text)
    expected = [
        "a",
        float("nan"),
        "NaN",
        float("inf"),
        "Infinity",
        -float("inf"),
        "-Infinity",
    ]
    for actual, wanted in zip(data, expected):
        assert actual == wanted or math.isnan(actual) and math.isnan(wanted)


def test_upstream_subset_special_singletons_decode():
    pos_inf = ujson_hpy.loads("Infinity")
    neg_inf = ujson_hpy.loads("-Infinity")
    nan = ujson_hpy.loads("NaN")
    null = ujson_hpy.loads("null")

    assert math.isinf(pos_inf) and pos_inf > 0
    assert math.isinf(neg_inf) and neg_inf < 0
    assert math.isnan(nan)
    assert null is None


def test_upstream_subset_nested_json_decode_error():
    with pytest.raises(ujson_hpy.JSONDecodeError):
        ujson_hpy.loads(b'{{"a":"b"}:"c"}')

    with pytest.raises(ujson_hpy.JSONDecodeError):
        ujson_hpy.loads('{"a":{"b":"c"}:"d"}')

    assert issubclass(ujson_hpy.JSONDecodeError, ValueError)


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        (1.0, "1.0"),
        (OrderedDict([(1, 1), (0, 0), (8, 8), (2, 2)]), '{"1":1,"0":0,"8":8,"2":2}'),
        ({"a": float("NaN")}, '{"a":NaN}'),
        ({"a": float("inf")}, '{"a":Infinity}'),
        ({"a": -float("inf")}, '{"a":-Infinity}'),
    ],
)
def test_upstream_subset_encode_cases(value, expected):
    assert ujson_hpy.encode(value) == expected


@pytest.mark.parametrize(
    "value",
    [
        [
            9223372036854775807,
            9223372036854775807,
            9223372036854775807,
            9223372036854775807,
            9223372036854775807,
            9223372036854775807,
        ],
        [
            18446744073709551615,
            18446744073709551615,
            18446744073709551615,
        ],
    ],
)
def test_upstream_subset_encode_list_long_conversion(value):
    output = ujson_hpy.encode(value)
    assert value == json.loads(output)
    assert value == ujson_hpy.decode(output)


@pytest.mark.parametrize("value", [9223372036854775807, 18446744073709551615])
def test_upstream_subset_encode_long_conversion(value):
    output = ujson_hpy.encode(value)

    assert value == json.loads(output)
    assert output == json.dumps(value)
    assert value == ujson_hpy.decode(output)


@pytest.mark.parametrize("value", [[[[[]]]], 31337, -31337, None, True, False])
def test_upstream_subset_encode_decode_roundtrip(value):
    output = ujson_hpy.encode(value)

    assert value == json.loads(output)
    assert output == json.dumps(value)
    assert value == ujson_hpy.decode(output)


@pytest.mark.parametrize(
    "value",
    [
        "Räksmörgås اسامة بن محمد بن عوض بن لادن",
        "\xe6\x97\xa5\xd1\x88",
        "\xf0\x90\x8d\x86",
        "\xf0\x91\x80\xb0TRAILINGNORMAL",
        "\xf3\xbf\xbf\xbfTRAILINGNORMAL",
    ],
)
def test_upstream_subset_encode_unicode_roundtrip(value):
    encoded = ujson_hpy.encode(value)
    decoded = ujson_hpy.decode(encoded)

    assert encoded == json.dumps(value)
    assert decoded == json.loads(encoded)


@pytest.mark.parametrize("ensure_ascii", [True, False])
def test_upstream_subset_encode_special_strings(ensure_ascii):
    assert (
        ujson_hpy.encode("/", ensure_ascii=ensure_ascii, escape_forward_slashes=False)
        == '"/"'
    )
    assert (
        ujson_hpy.encode("/", ensure_ascii=ensure_ascii, escape_forward_slashes=True)
        == '"\\/"'
    )
    assert ujson_hpy.encode('"', ensure_ascii=ensure_ascii) == '"\\""'


@pytest.mark.parametrize("ensure_ascii", [True, False])
def test_upstream_subset_encode_null_character(ensure_ascii):
    test_input = "31337 \x00 1337"
    output = ujson_hpy.encode(test_input, ensure_ascii=ensure_ascii)
    assert test_input == json.loads(output)
    assert output == json.dumps(test_input)
    assert test_input == ujson_hpy.decode(output)

    test_input = "\x00"
    output = ujson_hpy.encode(test_input, ensure_ascii=ensure_ascii)
    assert test_input == json.loads(output)
    assert output == json.dumps(test_input)
    assert test_input == ujson_hpy.decode(output)

    assert '"  \\u0000\\r\\n "' == ujson_hpy.dumps("  \u0000\r\n ")


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("1.7893", 1.7893),
        ("1.893", 1.893),
        ("1.3", 1.3),
        ("true", True),
        ("false", False),
        ("null", None),
        (" [ true, false,null] ", [True, False, None]),
    ],
)
def test_upstream_subset_loads_basic_values(text, expected):
    assert ujson_hpy.loads(text) == expected


def test_upstream_subset_decode_depth_limit_arrays():
    limit = 1025
    deep = "[" * limit + "1" + "]" * limit
    with pytest.raises(ujson_hpy.JSONDecodeError, match="[Dd]epth|[Ll]imit"):
        ujson_hpy.loads(deep)


def test_upstream_subset_decode_just_under_depth_limit():
    depth = 1024
    deep = "[" * depth + "1" + "]" * depth
    result = ujson_hpy.loads(deep)
    for _ in range(depth):
        result = result[0]
    assert result == 1


@pytest.mark.parametrize(
    "payload, error",
    [
        (bytes([34, 97, 0, 98, 34]).decode("latin1"), "Unmatched"),
        ('"\\\"', "Unmatched"),
        (b'"\xc3\x00"', "Invalid UTF-8 continuation"),
        (bytes([34, 92, 117, 48, 48, 101, 0, 34]).decode("latin1"), "Unterminated unicode escape sequence"),
        ('"\\u123z"', "Unexpected character in unicode escape"),
        (bytes([34, 92, 0, 34]).decode("latin1"), "Unterminated escape sequence"),
    ],
)
def test_upstream_subset_decode_invalid_string(payload, error):
    with pytest.raises(ujson_hpy.JSONDecodeError, match=error):
        ujson_hpy.loads(payload)


@pytest.mark.parametrize(
    "payload, error",
    [
        (b"\xfd", "Invalid UTF-8 sequence length"),
        (b"\xcf\x13", "Invalid UTF-8 continuation byte when decoding 'string'"),
        (b"\xc3", "Invalid UTF-8 continuation byte when decoding 'string'"),
        (b"\xc0\xa2", "Overlong 2-byte UTF-8 sequence"),
        (b"\xf4\x90\x80\x80", r"Code point > U\+10FFFF encountered"),
    ],
)
def test_upstream_subset_decode_bad_utf8_string(payload, error):
    with pytest.raises(ujson_hpy.JSONDecodeError, match=error) as capture:
        ujson_hpy.loads(b'"' + payload + b'"')
    with pytest.raises(capture.type, match=error):
        ujson_hpy.loads(b'"' + payload)


class ExampleIntEnum(enum.IntEnum):
    FOO = 42


class ExampleFloatEnum(float, enum.Enum):
    FOO = 3.1416


@pytest.mark.parametrize(
    "enum_class, expected",
    [(ExampleIntEnum, "42"), (ExampleFloatEnum, "3.1416")],
)
def test_upstream_subset_enum(enum_class, expected):
    assert ujson_hpy.dumps(enum_class.FOO) == expected


def test_upstream_subset_decode_number_with32bit_sign_bit():
    docs = (
        '{"id": 3590016419}',
        '{"id": %s}' % 2**31,
        '{"id": %s}' % 2**32,
        '{"id": %s}' % ((2**32) - 1),
    )
    results = (3590016419, 2**31, 2**32, 2**32 - 1)
    for doc, result in zip(docs, results):
        assert ujson_hpy.decode(doc)["id"] == result


def test_upstream_subset_loads_bytes_like():
    assert ujson_hpy.loads(b"123") == 123
    assert ujson_hpy.loads(memoryview(b'["a", "b", "c"]')) == ["a", "b", "c"]
    assert ujson_hpy.loads(bytearray(b"99")) == 99
    assert ujson_hpy.loads('"🦄🐳"'.encode()) == "🦄🐳"
    assert ujson_hpy.loads(array.array("B", b'{"a":1}')) == {"a": 1}


def test_upstream_subset_loads_non_c_contiguous():
    buffer = memoryview(b"".join(bytes([i]) + b"_" for i in b"[1, 2, 3]"))[::2]
    assert not buffer.c_contiguous
    assert ujson_hpy.loads(bytes(buffer)) == [1, 2, 3]
    with pytest.raises(TypeError):
        ujson_hpy.loads(buffer)


@pytest.mark.skipif(
    sys.implementation.name in ("pypy", "graalpy"),
    reason="PyPy & GraalPy use incompatible GC",
)
def test_upstream_subset_loads_bytes_like_refcounting():
    import gc

    gc.collect()
    buffer = b'{"a": 99}'
    old = sys.getrefcount(buffer)
    assert ujson_hpy.loads(buffer) == {"a": 99}
    assert sys.getrefcount(buffer) == old

    buffer = b'{"a": invalid}'
    old = sys.getrefcount(buffer)
    with pytest.raises(ValueError):
        ujson_hpy.loads(buffer)
    assert sys.getrefcount(buffer) == old


@pytest.mark.parametrize(
    "test_input, expected",
    [
        (r'"\uD83D\uDCA9"', "\U0001f4a9"),
        (r'"a\uD83D\uDCA9b"', "a\U0001f4a9b"),
        (r'"\uD800"', "\ud800"),
        (r'"a\uD800b"', "a\ud800b"),
        (r'"\uDEAD"', "\udead"),
        (r'"a\uDEADb"', "a\udeadb"),
        ('"\ud800"', "\ud800"),
        ('"\udead"', "\udead"),
        ('"\ud800a\udead"', "\ud800a\udead"),
        ('"\ud83d\udca9"', "\ud83d\udca9"),
    ],
)
def test_upstream_subset_decode_surrogate_characters(test_input, expected):
    assert ujson_hpy.loads(test_input) == expected
    assert ujson_hpy.loads(test_input.encode("utf-8", "surrogatepass")) == expected
    assert json.loads(test_input) == expected


def test_upstream_subset_reject_bytes_defaults():
    data = {"a": b"b"}
    with pytest.raises(TypeError):
        ujson_hpy.dumps(data)
    with pytest.raises(TypeError):
        ujson_hpy.dumps(data, reject_bytes=True)
    assert ujson_hpy.dumps(data, reject_bytes=False) == '{"a":"b"}'


@pytest.mark.parametrize(
    "value",
    [
        [b"hello"],
        {"a": {"b": [b"deep"]}},
    ],
)
def test_upstream_subset_reject_bytes_nested(value):
    with pytest.raises(TypeError, match="reject_bytes is on and"):
        ujson_hpy.dumps(value)


@pytest.mark.parametrize(
    "codepoint",
    [0x0, 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x10FFFF],
)
def test_upstream_subset_reject_bytes_false_codepoint_boundaries(codepoint):
    char = chr(codepoint)
    assert ujson_hpy.loads(ujson_hpy.dumps(char.encode(), reject_bytes=False)) == char


@pytest.mark.parametrize("indent", [999, 1000])
def test_upstream_subset_dump_huge_indent_roundtrip(indent):
    obj = {"list": [1, [2, 3], 4], "nested": {"key": "value", "a": True}}
    assert ujson_hpy.loads(ujson_hpy.encode(obj, indent=indent)) == obj


@pytest.mark.parametrize("indent", [1001, 1 << 30])
def test_upstream_subset_dump_too_large_indent_raises(indent):
    obj = {"list": [1, [2, 3], 4], "nested": {"key": "value", "a": True}}
    with pytest.raises((ValueError, OverflowError)):
        ujson_hpy.encode(obj, indent=indent)


def test_upstream_subset_negative_indent_behavior():
    obj = {"a": [1, 2], "b": "c"}
    assert ujson_hpy.dumps(obj) == '{"a":[1,2],"b":"c"}'
    assert ujson_hpy.dumps(obj, indent=0) == '{"a":[1,2],"b":"c"}'
    assert ujson_hpy.dumps(obj, indent=-1) == '{"a": [1,2],"b": "c"}'
    assert ujson_hpy.dumps(obj, indent=-1000000) == '{"a": [1,2],"b": "c"}'
    assert (
        ujson_hpy.dumps(obj, indent=2)
        == '{\n  "a": [\n    1,\n    2\n  ],\n  "b": "c"\n}'
    )


class _PlainObject:
    pass


def test_upstream_subset_default_function_fallthrough():
    assert (
        ujson_hpy.loads(ujson_hpy.dumps(_PlainObject(), default=lambda o: "fallback"))
        == "fallback"
    )


class DictTest:
    def toDict(self):
        return dict(key=31337)

    def __json__(self):
        return '"json defined"'


def test_upstream_subset_todict_takes_precedence_over_json():
    assert ujson_hpy.loads(ujson_hpy.dumps(DictTest())) == {"key": 31337}


class JSONTest:
    def __init__(self, output):
        self.output = output

    def __json__(self):
        return self.output


def test_upstream_subset_object_with_complex_json():
    obj = {"foo": ["bar", "baz"]}
    encoded = ujson_hpy.dumps({"key": JSONTest(ujson_hpy.dumps(obj))})
    assert ujson_hpy.loads(encoded) == {"key": obj}


class JSONTestAttributeError:
    def __json__(self):
        raise AttributeError


def test_upstream_subset_object_with_json_attribute_error():
    with pytest.raises(AttributeError):
        ujson_hpy.dumps({"key": JSONTestAttributeError()})


class JSONtoDictAttributeError:
    def toDict(self):
        raise AttributeError


def test_upstream_subset_object_with_todict_attribute_error():
    with pytest.raises(AttributeError):
        ujson_hpy.dumps({"key": JSONtoDictAttributeError()})


@pytest.mark.parametrize(
    "separators, expected",
    [
        (None, '{"a":0,"b":1}'),
        ((",", ":"), '{"a":0,"b":1}'),
        ((", ", ": "), '{"a": 0, "b": 1}'),
        (("\u203d", "\u00a1"), '{"a"\u00a10\u203d"b"\u00a11}'),
        (("i\x00", "k\x00"), '{"a"k\x000i\x00"b"k\x001}'),
        (("\udc80", "\udc81"), '{"a"\udc810\udc80"b"\udc811}'),
    ],
)
def test_upstream_subset_separators(separators, expected):
    assert ujson_hpy.dumps({"a": 0, "b": 1}, separators=separators) == expected


@pytest.mark.parametrize("value", [{}, []])
def test_upstream_subset_separators_empty_collection(value):
    assert ujson_hpy.dumps(value, separators=(" , ", " : ")) == ujson_hpy.dumps(value)


def test_upstream_subset_separators_with_indent_roundtrip():
    data = {"x": [1, 2], "y": 3}
    result = ujson_hpy.dumps(data, indent=2, separators=(",", ": "))
    assert ujson_hpy.loads(result) == data


def test_upstream_subset_sort_keys():
    data = {"a": 1, "c": 1, "b": 1, "e": 1, "f": 1, "d": 1}
    assert ujson_hpy.dumps(data, sort_keys=True) == '{"a":1,"b":1,"c":1,"d":1,"e":1,"f":1}'


def test_upstream_subset_sort_keys_unordered():
    data = {"a": 1, 1: 2, None: 3}
    assert ujson_hpy.dumps(data) == '{"a":1,"1":2,"null":3}'
    with pytest.raises(TypeError):
        ujson_hpy.dumps(data, sort_keys=True)
    with pytest.raises(TypeError):
        ujson_hpy.dumps([[0] * 100000, data], sort_keys=True)


def test_upstream_subset_encode_surrogate_characters():
    assert ujson_hpy.dumps("\udc7f") == r'"\udc7f"'
    out = r'{"\ud800":"\udfff"}'
    assert ujson_hpy.dumps({"\ud800": "\udfff"}) == out
    assert ujson_hpy.dumps({"\ud800": "\udfff"}, sort_keys=True) == out
    surrogate_bytes = {b"\xed\xa0\x80": b"\xed\xbf\xbf"}
    assert ujson_hpy.dumps(surrogate_bytes, reject_bytes=False) == out
    assert ujson_hpy.dumps(surrogate_bytes, reject_bytes=False, sort_keys=True) == out

    out2 = '{"\ud800":"\udfff"}'
    assert ujson_hpy.dumps({"\ud800": "\udfff"}, ensure_ascii=False) == out2
    assert ujson_hpy.dumps({"\ud800": "\udfff"}, ensure_ascii=False, sort_keys=True) == out2


@pytest.mark.skipif(
    sys.implementation.name in ("pypy", "graalpy"),
    reason="PyPy & GraalPy use incompatible GC",
)
def test_upstream_subset_encode_dict_values_ref_counting():
    import gc

    gc.collect()
    value = ["abc"]
    data = {"1": value}
    ref_count = sys.getrefcount(value)
    ujson_hpy.dumps(data)
    assert ref_count == sys.getrefcount(value)


@pytest.mark.skipif(
    sys.implementation.name in ("pypy", "graalpy"),
    reason="PyPy & GraalPy use incompatible GC",
)
@pytest.mark.parametrize("key", ["key", b"key", 1, True, False, None])
@pytest.mark.parametrize("sort_keys", [False, True])
def test_upstream_subset_encode_dict_key_ref_counting(key, sort_keys):
    import gc

    gc.collect()
    data = {key: "abc"}
    ref_count = sys.getrefcount(key)
    ujson_hpy.dumps(data, sort_keys=sort_keys)
    assert ref_count == sys.getrefcount(key)


@pytest.mark.skipif(
    sys.implementation.name == "graalpy",
    reason="GraalPy has an incompatible stub implementation of getrefcount",
)
@pytest.mark.parametrize("sort_keys", [False, True])
def test_upstream_subset_obj_str_exception_refcount(sort_keys):
    class Obj:
        def __str__(self):
            raise NotImplementedError

    key = Obj()
    old = sys.getrefcount(key)
    with pytest.raises(NotImplementedError):
        ujson_hpy.dumps({key: 1}, sort_keys=sort_keys)
    assert sys.getrefcount(key) == old


@pytest.mark.skipif(
    sys.implementation.name in ("pypy", "graalpy"),
    reason="PyPy & GraalPy use incompatible GC",
)
def test_upstream_subset_default_ref_counting():
    class DefaultRefCountingClass:
        def __init__(self, value):
            self._value = value

        def convert(self):
            if self._value > 1:
                return type(self)(self._value - 1)
            return 0

    import gc

    gc.collect()
    ujson_hpy.dumps(DefaultRefCountingClass(3), default=lambda x: x.convert())
    assert not any(
        type(o).__name__ == "DefaultRefCountingClass" for o in gc.get_objects()
    )


def test_upstream_subset_json_bytes_hook():
    class BytesJSON:
        def __json__(self):
            return b'{"source":"bytes"}'

    assert ujson_hpy.loads(ujson_hpy.dumps(BytesJSON())) == {"source": "bytes"}


def test_upstream_subset_dump_file_like_edgecases():
    class WritableFileLike:
        def __init__(self):
            self.bytes = ""

        def write(self, value):
            self.bytes += value

    class ZeroArgWriter:
        def write(self):
            return None

    buf = WritableFileLike()
    ujson_hpy.dump([1, 2, 3], buf)
    assert buf.bytes == "[1,2,3]"

    with pytest.raises(TypeError):
        ujson_hpy.dump([], "")

    with pytest.raises(TypeError, match="positional argument"):
        ujson_hpy.dump([0] * 3, ZeroArgWriter())


def test_upstream_subset_dump_closed_file():
    file = io.StringIO()
    file.close()
    with pytest.raises(ValueError, match="closed file"):
        ujson_hpy.dump([0] * 100, file)


def test_upstream_subset_load_bad_arguments():
    with pytest.raises(TypeError):
        ujson_hpy.load(object())

    file = type("File", (), {})()
    file.read = object()
    with pytest.raises(TypeError, match="expected file"):
        ujson_hpy.load(file)

    file.read = lambda x: None
    with pytest.raises(TypeError, match="missing 1 required positional"):
        ujson_hpy.load(file)

    file.read = lambda: 1 / 0
    with pytest.raises(ZeroDivisionError):
        ujson_hpy.load(file)

    file.read = lambda: 3
    with pytest.raises(TypeError):
        ujson_hpy.load(file)


def test_upstream_subset_load_closed_file():
    file = io.StringIO("[]")
    file.close()
    with pytest.raises(ValueError, match="closed file"):
        ujson_hpy.load(file)


def test_upstream_subset_default_callback_ascii_and_non_ascii():
    assert (
        ujson_hpy.dumps({"x": object()}, ensure_ascii=True, default=lambda o: "ascii: abcXYZ")
        == '{"x":"ascii: abcXYZ"}'
    )
    assert (
        ujson_hpy.dumps(
            {"x": object()},
            ensure_ascii=True,
            default=lambda o: "non_ascii: 生日快乐",
        )
        == '{"x":"non_ascii: \\u751f\\u65e5\\u5feb\\u4e50"}'
    )


class BadToDict:
    def __init__(self, callback):
        self.callback = callback

    def toDict(self):
        return self.callback()


def test_upstream_subset_bad_todict():
    with pytest.raises(ZeroDivisionError):
        ujson_hpy.dumps(BadToDict(lambda: 1 / 0))
    with pytest.raises(TypeError, match=r"toDict\(\) should return a dict"):
        ujson_hpy.dumps(BadToDict(lambda: 3))
    with pytest.raises(TypeError, match="object object .* not JSON serializable"):
        ujson_hpy.dumps(BadToDict(lambda: {"a": object()}))


def test_upstream_subset_comprehensive_json_fixture():
    fixture = Path(__file__).with_name("comprehensive.json")
    raw = fixture.read_bytes()

    data = ujson_hpy.loads(raw)
    stdlib_data = json.loads(raw)
    assert data == stdlib_data
    assert ujson_hpy.loads(ujson_hpy.dumps(data)) == data

    nums = data["numbers"]
    assert isinstance(nums["zero"], int) and nums["zero"] == 0
    assert isinstance(nums["forty_two"], int) and nums["forty_two"] == 42
    assert isinstance(nums["negative_one"], int) and nums["negative_one"] == -1
    assert isinstance(nums["pi"], float) and math.isclose(nums["pi"], math.pi, rel_tol=1e-12)
    assert isinstance(nums["half"], float) and nums["half"] == 0.5

    bools = data["booleans"]
    assert bools["yes"] is True
    assert bools["no"] is False
    assert data["null_value"] is None

    escapes = data["strings"]["standard_escapes"]
    assert "\t" in escapes
    assert "\n" in escapes
    assert "\r" in escapes
    assert "\b" in escapes
    assert "\f" in escapes
    assert "\\" in escapes
    assert '"' in escapes
    assert "/" in escapes

    assert "é" in data["strings"]["unicode_latin"]
    assert "中" in data["strings"]["unicode_cjk"]
    assert "😀" in data["strings"]["unicode_emoji"]

    arrs = data["arrays"]
    assert arrs["empty"] == []
    assert arrs["single"] == [42]
    assert arrs["integers"] == list(range(1, 11))
    assert arrs["nested_4"][0][0][0] == [1, 2]
    assert arrs["of_objects"][1]["val"] == "b"

    objs = data["objects"]
    assert objs["empty"] == {}
    assert objs["single_key"] == {"only": "value"}
    assert objs["flat"]["d"] is None
    deep = objs["nested_6"]["l1"]["l2"]["l3"]["l4"]["l5"]["l6"]
    assert deep == "six levels deep"

    alice = data["records"][0]
    assert alice["name"] == "Alice"
    assert alice["active"] is True
    assert alice["score"] == 98.5
    assert alice["prefs"]["limits"]["rate"] == 10.5
    assert alice["history"][1]["action"] == "upload"
    assert alice["metadata"] is None

    bob = data["records"][1]
    assert bob["active"] is False
    assert bob["tags"] == []
    assert bob["metadata"]["flags"] == [1, 2, 4]

    assert data["matrix"][3][4] == 20
    assert sum(data["matrix"][0]) == 15

    cfg = data["config"]
    assert cfg["features"]["beta"]["suboptions"]["backoff"] == [1.0, 2.0, 4.0, 8.0]
    assert cfg["limits"]["burst"] is None
    assert "example.com" in cfg["allowed_origins"][0]

    nan_doc = '{"values": [NaN, Infinity, -Infinity]}'
    tol = ujson_hpy.loads(nan_doc)
    assert math.isnan(tol["values"][0])
    assert math.isinf(tol["values"][1]) and tol["values"][1] > 0
    assert math.isinf(tol["values"][2]) and tol["values"][2] < 0
    stdlib_tol = json.loads(nan_doc)
    assert math.isnan(stdlib_tol["values"][0])
    assert tol["values"][1] == stdlib_tol["values"][1]
    assert tol["values"][2] == stdlib_tol["values"][2]

    with pytest.raises(json.JSONDecodeError):
        json.loads("01")
    assert ujson_hpy.loads("01") == 1

    with pytest.raises(json.JSONDecodeError):
        json.loads("1.")
    assert ujson_hpy.loads("1.") == 1.0
