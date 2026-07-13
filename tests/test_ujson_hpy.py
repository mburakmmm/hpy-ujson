import array
import io
import re

import pytest

try:
    from hpy.debug import LeakDetector
except Exception:  # pragma: no cover - optional dependency path
    LeakDetector = None


def test_ujson_hpy_imports():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert ujson_hpy.__version__
    assert issubclass(ujson_hpy.JSONDecodeError, ValueError)
    assert re.search(r"^\d+\.\d+(\.\d+)?", ujson_hpy.__version__)


def test_ujson_hpy_reports_abi():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert ujson_hpy._hpy_abi() in {"cpython", "hybrid", "universal"}


def require_ujson_hpy_universal():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    if ujson_hpy._hpy_abi() != "universal":
        pytest.skip("requires universal HPy build")
    if LeakDetector is None:
        pytest.skip("hpy.debug is not available")
    return ujson_hpy


@pytest.mark.parametrize(
    ("name", "payload", "expected"),
    [
        ("loads", '{"a":1,"b":[true,false,null]}', {"a": 1, "b": [True, False, None]}),
        ("decode", '{"a":1,"b":[true,false,null]}', {"a": 1, "b": [True, False, None]}),
    ],
)
def test_ujson_hpy_decode_methods_work_for_str(name, payload, expected):
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert getattr(ujson_hpy, name)(payload) == expected


def test_ujson_hpy_raises_custom_decode_error():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(ujson_hpy.JSONDecodeError):
        ujson_hpy.loads("{")


@pytest.mark.parametrize(
    ("payload", "expected"),
    [
        (b'{"value": 99}', {"value": 99}),
        (bytearray(b"[1, 2, 3]"), [1, 2, 3]),
        (memoryview(b'["a", "b", "c"]'), ["a", "b", "c"]),
        (array.array("B", b'{"a": 1}'), {"a": 1}),
    ],
)
def test_ujson_hpy_loads_accepts_bytes_like_inputs(payload, expected):
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert ujson_hpy.loads(payload) == expected


def test_ujson_hpy_loads_rejects_non_c_contiguous_buffers():
    ujson_hpy = pytest.importorskip("ujson_hpy")
    payload = memoryview(b"".join(bytes([i]) + b"_" for i in b"[1, 2, 3]"))[::2]

    assert not payload.c_contiguous
    with pytest.raises(TypeError, match="C-contiguous bytes-like object"):
        ujson_hpy.loads(payload)


@pytest.mark.parametrize(
    ("fp", "expected"),
    [
        (io.StringIO('{"answer": 42}'), {"answer": 42}),
        (io.BytesIO(b'{"answer": 42}'), {"answer": 42}),
    ],
)
def test_ujson_hpy_load_reads_from_file_like_objects(fp, expected):
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert ujson_hpy.load(fp) == expected


def test_ujson_hpy_load_accepts_file_like_memoryview_payload():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class BufferFile:
        def read(self):
            return memoryview(b'{"answer": 42}')

    assert ujson_hpy.load(BufferFile()) == {"answer": 42}


def test_ujson_hpy_load_rejects_non_file_like_objects():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(TypeError, match="expected file"):
        ujson_hpy.load(object())


def test_ujson_hpy_load_rejects_non_callable_read_attribute():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class NotAFile:
        read = object()

    with pytest.raises(TypeError, match="expected file"):
        ujson_hpy.load(NotAFile())


def test_ujson_hpy_loads_rejects_unsupported_input():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(TypeError, match="Expected string, bytes, or bytearray"):
        ujson_hpy.loads(object())


@pytest.mark.parametrize(
    ("name", "payload", "expected"),
    [
        ("dumps", {"b": [1, 2], "a": True}, '{"b":[1,2],"a":true}'),
        ("encode", {"b": [1, 2], "a": True}, '{"b":[1,2],"a":true}'),
    ],
)
def test_ujson_hpy_encode_methods_work_for_basic_objects(name, payload, expected):
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert getattr(ujson_hpy, name)(payload) == expected


def test_ujson_hpy_dumps_supports_sort_keys_and_indent():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    assert ujson_hpy.dumps({"b": 1, "a": 2}, sort_keys=True) == '{"a":2,"b":1}'
    assert ujson_hpy.dumps({"a": [1, 2], "b": "c"}, indent=-1) == '{"a": [1,2],"b": "c"}'
    assert ujson_hpy.dumps({"a": [1, 2]}, indent=2) == '{\n  "a": [\n    1,\n    2\n  ]\n}'


def test_ujson_hpy_dumps_supports_default_callback():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class Example:
        pass

    assert ujson_hpy.dumps(Example(), default=lambda _: {"ok": 1}) == '{"ok":1}'


def test_ujson_hpy_dumps_preserves_default_exception_type():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(RuntimeError, match="boom"):
        ujson_hpy.dumps(object(), default=lambda _: (_ for _ in ()).throw(RuntimeError("boom")))


def test_ujson_hpy_dumps_rejects_recursive_default_loop():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class Loop:
        pass

    with pytest.raises(TypeError, match="maximum recursion depth exceeded"):
        ujson_hpy.dumps(Loop(), default=lambda value: value)


def test_ujson_hpy_dump_writes_to_file_like_object():
    ujson_hpy = pytest.importorskip("ujson_hpy")
    buf = io.StringIO()

    assert ujson_hpy.dump({"a": [1, 2]}, buf) is None
    assert buf.getvalue() == '{"a":[1,2]}'


def test_ujson_hpy_dump_propagates_write_exceptions():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class BadFile:
        def write(self, value):
            raise ZeroDivisionError("boom")

    with pytest.raises(ZeroDivisionError, match="boom"):
        ujson_hpy.dump([1], BadFile())


def test_ujson_hpy_reject_bytes_flag_matches_basic_behavior():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(TypeError):
        ujson_hpy.dumps(b"abc")

    assert ujson_hpy.dumps(b"abc", reject_bytes=False) == '"abc"'


def test_ujson_hpy_rejects_invalid_utf8_bytes_when_serializing():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises((UnicodeDecodeError, OverflowError)):
        ujson_hpy.dumps(b"\xff", reject_bytes=False)


def test_ujson_hpy_supports_decimal_special_keys_and_separators():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    import decimal

    assert ujson_hpy.dumps(decimal.Decimal("1.25")) == "1.25"
    assert ujson_hpy.dumps({None: 0, True: 1, False: 2}) == '{"null":0,"true":1,"false":2}'
    assert ujson_hpy.dumps({"a": 0, "b": 1}, separators=(", ", ": ")) == '{"a": 0, "b": 1}'


@pytest.mark.parametrize(
    ("separators", "expected_exception"),
    [
        (True, TypeError),
        ((), ValueError),
        ((",", True), TypeError),
        ((",", ":", "x"), ValueError),
    ],
)
def test_ujson_hpy_validates_separators(separators, expected_exception):
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(expected_exception):
        ujson_hpy.dumps({"a": 0, "b": 1}, separators=separators)


def test_ujson_hpy_respects_allow_nan_flag():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    with pytest.raises(OverflowError, match="Invalid value when encoding double"):
        ujson_hpy.dumps(float("nan"), allow_nan=False)


def test_ujson_hpy_supports_json_and_todict_hooks():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class JsonValue:
        def __json__(self):
            return '{"nested":true}'

    class DictValue:
        def toDict(self):
            return {"ok": 1}

    assert ujson_hpy.dumps(JsonValue()) == '{"nested":true}'
    assert ujson_hpy.dumps(DictValue()) == '{"ok":1}'


def test_ujson_hpy_validates_todict_and_json_hook_return_types():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class BadJson:
        def __json__(self):
            return 3

    class BadToDict:
        def __init__(self, callback):
            self.callback = callback

        def toDict(self):
            return self.callback()

    with pytest.raises(TypeError, match="__json__\\(\\) should return str or bytes"):
        ujson_hpy.dumps(BadJson())

    with pytest.raises(TypeError, match="toDict\\(\\) should return a dict"):
        ujson_hpy.dumps(BadToDict(lambda: 3))


def test_ujson_hpy_does_not_serialize_arbitrary___float___objects():
    ujson_hpy = pytest.importorskip("ujson_hpy")

    class HasFloat:
        def __float__(self):
            return 1.25

    with pytest.raises(TypeError, match="is not JSON serializable"):
        ujson_hpy.dumps(HasFloat())


def test_ujson_hpy_universal_decode_does_not_leak_handles():
    ujson_hpy = require_ujson_hpy_universal()

    with LeakDetector():
        assert ujson_hpy.loads('{"a":[1,true,null]}') == {"a": [1, True, None]}


def test_ujson_hpy_universal_encode_does_not_leak_handles():
    ujson_hpy = require_ujson_hpy_universal()

    with LeakDetector():
        assert ujson_hpy.dumps({"a": [1, 2], "b": True}) == '{"a":[1,2],"b":true}'


def test_ujson_hpy_universal_default_callback_does_not_leak_handles():
    ujson_hpy = require_ujson_hpy_universal()

    with LeakDetector():
        assert (
            ujson_hpy.dumps({"x": object()}, ensure_ascii=True, default=lambda o: "ascii: abcXYZ")
            == '{"x":"ascii: abcXYZ"}'
        )

    with LeakDetector():
        assert (
            ujson_hpy.dumps(
                {"x": object()},
                ensure_ascii=True,
                default=lambda o: "non_ascii: 生日快乐",
            )
            == '{"x":"non_ascii: \\u751f\\u65e5\\u5feb\\u4e50"}'
        )


def test_ujson_hpy_universal_dump_does_not_leak_handles():
    ujson_hpy = require_ujson_hpy_universal()
    buf = io.StringIO()

    with LeakDetector():
        assert ujson_hpy.dump({"a": [1, 2]}, buf) is None
    assert buf.getvalue() == '{"a":[1,2]}'
