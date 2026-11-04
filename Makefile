# Convenience targets. The build itself is CMake (see AGENTS.md).
BUILD_DIR ?= build

.PHONY: models models-build
models:          ## fetch/verify/unpack model artefacts (models/manifest.json)
	python3 tools/models.py --build-dir $(BUILD_DIR)

models-build:    ## same, building the OpenKeyScan sidecar from the pinned source if needed
	python3 tools/models.py --build-dir $(BUILD_DIR) --build
