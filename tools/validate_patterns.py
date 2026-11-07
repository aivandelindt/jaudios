#!/usr/bin/env python3
"""Validate engine/generators/patterns/*.json against patterns/schema.json (bass-generator skill:
"Pattern JSON validated against schema in CI"). Stdlib only.

    python3 tools/validate_patterns.py [PATTERN_DIR]

Implements the JSON Schema keywords schema.json uses (type, required, properties,
additionalProperties, enum, minimum, maximum, minItems, items, pattern), then the rules a schema
cannot express: every step and its length inside the loop (grid * bars), no overlapping notes (a
bass line is monophonic), unique names. Exits 1 on the first file with errors, after listing all.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TYPES = {"object": dict, "array": list, "string": str, "number": (int, float), "integer": int}


def validate(value, schema, path, errors):
    t = schema.get("type")
    if t:
        ok = isinstance(value, TYPES[t]) and not (t in ("number", "integer") and isinstance(value, bool))
        if not ok:
            errors.append(f"{path}: expected {t}, got {type(value).__name__}")
            return
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} not one of {schema['enum']}")
    if "minimum" in schema and value < schema["minimum"]:
        errors.append(f"{path}: {value} < minimum {schema['minimum']}")
    if "maximum" in schema and value > schema["maximum"]:
        errors.append(f"{path}: {value} > maximum {schema['maximum']}")
    if "pattern" in schema and not re.search(schema["pattern"], value):
        errors.append(f"{path}: {value!r} does not match {schema['pattern']}")
    if t == "object":
        props = schema.get("properties", {})
        for key in schema.get("required", []):
            if key not in value:
                errors.append(f"{path}: missing required '{key}'")
        for key, v in value.items():
            if key in props:
                validate(v, props[key], f"{path}.{key}", errors)
            elif schema.get("additionalProperties") is False:
                errors.append(f"{path}: unexpected property '{key}'")
    if t == "array":
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{path}: fewer than {schema['minItems']} items")
        for i, item in enumerate(value):
            validate(item, schema.get("items", {}), f"{path}[{i}]", errors)


def semantic(p, errors):
    if errors:  # structure must be right first
        return
    total = p["grid"] * p["bars"]
    end = -1
    for i, s in enumerate(sorted(p["steps"], key=lambda s: s["step"])):
        length = s.get("length", 1)
        if s["step"] >= total:
            errors.append(f"steps[{i}]: step {s['step']} outside the loop ({total} steps)")
        if s["step"] + length > total:
            errors.append(f"steps[{i}]: step {s['step']} + length {length} runs past the loop ({total} steps)")
        if s["step"] < end:
            errors.append(f"steps[{i}]: step {s['step']} overlaps the previous note (ends at {end})")
        end = s["step"] + length


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "engine" / "generators" / "patterns"
    schema = json.loads((folder / "schema.json").read_text())
    names, failed = set(), False
    files = sorted(f for f in folder.glob("*.json") if f.name != "schema.json")
    if not files:
        sys.exit(f"no patterns in {folder}")
    for f in files:
        errors = []
        try:
            p = json.loads(f.read_text())
        except ValueError as e:
            errors.append(f"not valid JSON: {e}")
            p = None
        if p is not None:
            validate(p, schema, "$", errors)
            semantic(p, errors)
            if not errors:
                if p["name"] in names:
                    errors.append(f"duplicate name {p['name']}")
                if f.stem != p["name"]:
                    errors.append(f"file name {f.stem} != pattern name {p['name']}")
                names.add(p["name"])
        status = "ok" if not errors else "FAIL"
        print(f"{status:4s} {f.name}" + ("" if not errors else "\n     " + "\n     ".join(errors)))
        failed = failed or bool(errors)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
