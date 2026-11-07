#!/usr/bin/env python3
"""Validate generator pattern JSON against the schema.json in its folder (bass-generator skill:
"Pattern JSON validated against schema in CI"; drum-generator patterns use the same check). Stdlib only.

    python3 tools/validate_patterns.py [PATTERN_DIR ...]   (default: engine/generators/patterns and .../drums)

Implements the JSON Schema keywords the schemas use (type, required, properties,
additionalProperties, enum, minimum, maximum, minItems, items, pattern, $ref to #/$defs), then the
rules a schema cannot express. Bass: every step and its length inside the loop (grid * bars), no
overlapping notes (a bass line is monophonic). Drums: every main lane spans bars * 16 steps, every
variant lane one bar (or is empty), at least one hit. Both: unique names, file name == name.
Exits 1 if any file has errors, after listing all.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TYPES = {"object": dict, "array": list, "string": str, "number": (int, float), "integer": int}


def validate(value, schema, path, errors, root=None):
    root = root or schema
    if "$ref" in schema:
        ref = schema["$ref"]
        assert ref.startswith("#/$defs/"), ref
        return validate(value, root["$defs"][ref[len("#/$defs/"):]], path, errors, root)
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
                validate(v, props[key], f"{path}.{key}", errors, root)
            elif schema.get("additionalProperties") is False:
                errors.append(f"{path}: unexpected property '{key}'")
    if t == "array":
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{path}: fewer than {schema['minItems']} items")
        for i, item in enumerate(value):
            validate(item, schema.get("items", {}), f"{path}[{i}]", errors, root)


def semantic_drums(p, errors):
    if errors:
        return
    total = 16 * p["bars"]
    for lane, steps in p["lanes"].items():
        if len(steps) != total:
            errors.append(f"lanes.{lane}: {len(steps)} steps, expected {total} (bars * 16)")
    if not any(c != "." for steps in p["lanes"].values() for c in steps):
        errors.append("lanes: no hits")
    for variant in ("fill", "riser_hit", "drop", "half_time"):
        for lane, steps in p.get(variant, {}).items():
            if len(steps) not in (0, 16):
                errors.append(f"{variant}.{lane}: {len(steps)} steps, expected one bar (16) or empty")


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
    folders = [Path(a) for a in sys.argv[1:]] or [ROOT / "engine" / "generators" / "patterns",
                                                ROOT / "engine" / "generators" / "drums"]
    failed = False
    for folder in folders:
        failed = check_folder(folder) or failed
    sys.exit(1 if failed else 0)


def check_folder(folder):
    schema = json.loads((folder / "schema.json").read_text())
    rules = semantic_drums if "drum" in schema.get("$id", "") else semantic
    names, failed = set(), False
    files = sorted(f for f in folder.glob("*.json") if f.name != "schema.json")
    if not files:
        sys.exit(f"no patterns in {folder}")
    print(f"{folder.relative_to(ROOT) if folder.is_relative_to(ROOT) else folder} ({schema.get('$id')})")
    for f in files:
        errors = []
        try:
            p = json.loads(f.read_text())
        except ValueError as e:
            errors.append(f"not valid JSON: {e}")
            p = None
        if p is not None:
            validate(p, schema, "$", errors)
            rules(p, errors)
            if not errors:
                if p["name"] in names:
                    errors.append(f"duplicate name {p['name']}")
                if f.stem != p["name"]:
                    errors.append(f"file name {f.stem} != pattern name {p['name']}")
                names.add(p["name"])
        status = "ok" if not errors else "FAIL"
        print(f"  {status:4s} {f.name}" + ("" if not errors else "\n       " + "\n       ".join(errors)))
        failed = failed or bool(errors)
    return failed


if __name__ == "__main__":
    main()
