BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python
UV ?= uv

.PHONY: all build configure clean check wheel

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	$(CMAKE) --build $(BUILD_DIR)

check: build
	$(PYTHON) -m compileall -q src/uvl
	PYTHONPATH=src $(PYTHON) -m uvl --help >/dev/null
	$(BUILD_DIR)/uvl --help >/dev/null

wheel:
	$(UV) build --wheel

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR) dist
	find src/uvl -type d -name __pycache__ -prune -exec rm -rf {} +
	find src/uvl -type f -name '*.pyc' -delete
