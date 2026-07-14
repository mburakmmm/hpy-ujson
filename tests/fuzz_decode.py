"""Deterministic smoke fuzzer for a JSON decoder's native input paths."""

import argparse
import importlib
import random
import re


class RangeOption(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        values = re.findall("[^: ]+", values)
        if len(values) == 1:
            values = (int(values[0]),)
        else:
            values = range(*map(int, values))
        setattr(namespace, self.dest, values)


def mutated_payload(randomizer):
    corpus = [
        b"null",
        b'{"answer":42,"items":[true,false,null]}',
        '"merhaba \\ud83d\\ude80"'.encode(),
        b"[0,-1,1.5,1e100]",
    ]
    payload = bytearray(randomizer.choice(corpus))
    for _ in range(randomizer.randrange(1, 8)):
        operation = randomizer.randrange(3)
        position = randomizer.randrange(len(payload) + 1)
        if operation == 0 and payload:
            del payload[min(position, len(payload) - 1)]
        elif operation == 1 and payload:
            payload[min(position, len(payload) - 1)] = randomizer.randrange(256)
        else:
            payload.insert(position, randomizer.randrange(256))
    return bytes(payload)


def fuzz(module_name, seeds):
    target = importlib.import_module(module_name)
    for seed in seeds:
        print(f"--seed {seed}", flush=True)
        randomizer = random.Random(seed)
        random_bytes = randomizer.randbytes(randomizer.randrange(513))
        mutated = mutated_payload(randomizer)
        inputs = (
            random_bytes,
            bytearray(random_bytes),
            memoryview(random_bytes),
            mutated,
            mutated.decode("utf-8", errors="replace"),
        )
        for value in inputs:
            try:
                target.loads(value)
            except Exception:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--module",
        choices=("ujson", "ujson_hpy"),
        default="ujson_hpy",
    )
    parser.add_argument(
        "--seed",
        default=range(100),
        action=RangeOption,
        dest="seeds",
        help="A seed or range in start:end[:step] form.",
    )
    args = parser.parse_args()
    fuzz(args.module, args.seeds)


if __name__ == "__main__":
    main()
