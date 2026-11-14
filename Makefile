SHELL := /bin/bash
.DEFAULT_GOAL := help

ESPPORT ?= /dev/ttyUSB0
PORT ?= $(ESPPORT)
IDF_DOCKER_IMAGE ?= espressif/idf:release-v5.5
IDF_IMAGE ?= $(IDF_DOCKER_IMAGE)
IDF ?= $(shell command -v idf.py 2>/dev/null)
NPM ?= npm
CMAKE ?= cmake
PYTHON ?= python3
STANDBY_TOOLS_DIR ?= $(CURDIR)/build/standby-tools
STANDBY_PYTHON ?= $(STANDBY_TOOLS_DIR)/bin/python
STANDBY_TOOLS_STAMP := $(STANDBY_TOOLS_DIR)/.installed
SCREENSHOT_DIR ?= docs/ui/screenshots
SCREENSHOT_CHECK_DIR ?= /tmp/kaffeekasse-screenshots

.PHONY: help bootstrap build build-native build-docker flash flash-docker monitor \
	test test-host test-web test-e2e e2e hil web web-dev mock sim sim-build \
	screenshots screenshots-check test-ui fmt fmt-check lint docs assets tools-deps \
	standby-tools standby-cup-check test-standby-cup check clean

help:
	@echo "Kaffeekasse targets:"
	@echo "  bootstrap     check tools and install web dependencies"
	@echo "  build         build firmware (local idf.py or Docker)"
	@echo "  assets        embed web/dist into the firmware (tools/embed_web.py)"
	@echo "  flash         flash $(PORT)"
	@echo "  monitor       serial monitor"
	@echo "  web           build admin SPA"
	@echo "  web-dev       Vite + mock API"
	@echo "  sim           build and run the LVGL simulator"
	@echo "  screenshots   regenerate docs/ui/screenshots from the simulator"
	@echo "  test-ui       LVGL click/hit-test smoke test (SDL2, no board)"
	@echo "  test          host + web tests"
	@echo "  e2e           Playwright admin UI tests"
	@echo "  hil           hardware-in-the-loop smoke test ($(PORT))"
	@echo "  check         format, lint, docs, tests, builds, size budgets"
	@echo "  fmt           clang-format C++; TypeScript is typechecked"
	@echo "  tools-deps    install Python HIL/lint dependencies"
	@echo "  clean         remove build output"

bootstrap:
	@bash tools/bootstrap.sh
	@$(PYTHON) -c 'import yaml' 2>/dev/null || \
		echo "warn: PyYAML missing; run 'make tools-deps' for OpenAPI lint/HIL"

tools-deps:
	$(PYTHON) -m pip install -r tools/requirements-tools.txt

standby-tools: $(STANDBY_TOOLS_STAMP)

$(STANDBY_TOOLS_STAMP): tools/requirements-standby.txt
	$(PYTHON) -m venv $(STANDBY_TOOLS_DIR)
	$(STANDBY_PYTHON) -m pip install --disable-pip-version-check -r $<
	@touch $@

test-standby-cup: standby-tools
	$(STANDBY_PYTHON) tools/test_generate_standby_cup.py

standby-cup-check: standby-tools test-standby-cup
	$(STANDBY_PYTHON) tools/generate_standby_cup.py --output $(CURDIR)/build/standby-cup-check/standby_cup_generated.hpp
	$(STANDBY_PYTHON) tools/generate_standby_cup.py --check --output $(CURDIR)/build/standby-cup-check/standby_cup_generated.hpp

web/node_modules: web/package-lock.json
	$(NPM) --prefix web ci

web: web/node_modules
	$(NPM) --prefix web run build

assets: web
	$(PYTHON) tools/embed_web.py
	$(PYTHON) tools/check_avatar_i4.py

web-dev: web/node_modules
	@bash -c '$(NPM) --prefix web run mock & MOCK_PID=$$!; trap "kill $$MOCK_PID" EXIT; $(NPM) --prefix web run dev'

mock: web/node_modules
	$(NPM) --prefix web run mock

test-host:
	$(CMAKE) -S tests/host -B tests/host/build -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build tests/host/build -j
	./tests/host/build/kaffee_tests

test-web: web/node_modules
	$(NPM) --prefix web run typecheck
	$(NPM) --prefix web test

test: test-host test-web

test-e2e: web/node_modules
	$(NPM) --prefix web run e2e

e2e: test-e2e

hil:
	$(PYTHON) tools/hil.py --port $(PORT)

build-native: assets standby-tools
	@if [ -z "$(IDF)" ]; then echo "idf.py not found; use 'make build-docker' or install ESP-IDF 5.5"; exit 1; fi
	cd firmware && STANDBY_PYTHON="$(STANDBY_PYTHON)" idf.py build

build-docker: assets
	docker run --rm -v "$(CURDIR):/repo" -w /repo $(IDF_IMAGE) \
		bash /repo/tools/build_firmware_container.sh

build:
	@if [ -n "$(IDF)" ]; then $(MAKE) build-native; else $(MAKE) build-docker; fi

flash:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) flash; else \
		docker run --rm -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) \
			$(IDF_IMAGE) bash -lc '. $$IDF_PATH/export.sh && idf.py -p $(PORT) flash'; fi

flash-docker: flash

monitor:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) monitor; else \
		docker run --rm -it -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) \
			$(IDF_IMAGE) bash -lc '. $$IDF_PATH/export.sh && idf.py -p $(PORT) monitor'; fi

sim-build: standby-tools
	STANDBY_PYTHON="$(STANDBY_PYTHON)" $(CMAKE) -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build sim/build -j

sim: sim-build
	./sim/build/kaffeekasse-sim

screenshots: sim-build
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/01-idle.png --page 1 --state idle
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/02-user-selected.png --page 1 --state selected
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/03-booking-toast.png --page 1 --state toast
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/04-leaderboard.png --page 2 --state idle
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/05-system-ap-qr.png --page 3 --state ap
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/06-page1-15-users.png --page 1 --users 15
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/07-page1-30-users-page3.png --page 1 --users 30 --book-page 2
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/08-page1-30-users-usage-order.png --page 1 --users 30 --order usage
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/06-coffee-standby.png --page 1 --state standby

screenshots-check: sim-build
	rm -rf $(SCREENSHOT_CHECK_DIR)
	mkdir -p $(SCREENSHOT_CHECK_DIR)
	$(MAKE) screenshots SCREENSHOT_DIR=$(SCREENSHOT_CHECK_DIR)
	python3 tools/check_screenshots.py docs/ui/screenshots $(SCREENSHOT_CHECK_DIR)

test-ui: sim-build
	./sim/build/kaffeekasse-sim --click-test

fmt:
	@if command -v clang-format >/dev/null; then \
		find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/_deps' -o -path '*/managed_components' -o -path 'firmware/components/webassets/generated' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null | xargs -r clang-format -i; \
	fi
	$(NPM) --prefix web run typecheck

fmt-check:
	@if command -v clang-format >/dev/null; then \
		files=$$(find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/_deps' -o -path '*/managed_components' -o -path 'firmware/components/webassets/generated' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null); \
		clang-format --dry-run --Werror $$files; \
	else echo "clang-format not installed, skipping"; fi

lint:
	$(PYTHON) tools/lint_openapi.py docs/api/openapi.yaml
	$(NPM) --prefix web run typecheck

docs:
	$(PYTHON) tools/check_docs.py

check: fmt-check lint docs test web assets standby-cup-check
	@if command -v idf.py >/dev/null; then cd firmware && STANDBY_PYTHON="$(STANDBY_PYTHON)" idf.py build; else echo "Skipping firmware build (idf.py not found; CI builds it in Docker)"; fi
	@$(PYTHON) tools/check_sizes.py

clean:
	rm -rf firmware/build tests/host/build sim/build web/dist web/node_modules \
		firmware/components/webassets/generated
