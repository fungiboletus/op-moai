# moai: a synth engine for the OP-1 running op1r (an op1r plugin).
#
# The plugin source lives in plugin/. The op1r SDK (submodule op1r-src) builds plugins only from inside its own tree
# (the cross compiler runs in Docker with the SDK mounted), so every target first copies plugin/ to
# op1r-src/synth_sdk/plugins/moai/ and works there. Edit plugin/, never the copy.
#
#   make setup       Python venv (numpy, scipy, pyelftools, pycairo) + the Docker cross compiler (once, ~30 min)
#   make             the plugin file -> build/$(NAME).op1p
#   make test        host tests (sanitizers): levels, filter, oscillators, contour, views, fuzz, presets
#   make bench       Blackfin cycles per sample, host == Blackfin output (needs `make sim` once)
#   make cachetrace  the cache lines one audio callback touches (three held notes; needs `make sim` once)
#   make preview     screen preview -> build/preview.png
#   make demo        a demo phrase -> build/demo.wav
#   make screens     one image per screen state -> docs/screens/ (and its README.md)
#   make dist        plugin + preview + demo (what a GitHub release carries: .github/workflows/build.yml)
#   make tables      regenerate moai_tables.h / moai.svg / moai_geom.h / moai_presets.h
#
# NAME is the engine name on the OP-1 (<= 15 chars, lowercase). The OP-1 tells plugins apart by this name, so a build
# with another name installs next to the existing one (A/B on the device) instead of replacing it; a sound saved with
# one engine name does not open with another.
NAME    ?= moai
SDK     := op1r-src/synth_sdk
DST     := $(SDK)/plugins/moai
VENV    := .venv
PY      := $(abspath $(VENV))/bin/python
DOCKER_IMAGE := bfin-elf-toolchain
SIM_IMAGE    := bfin-elf-sim

.PHONY: all setup venv toolchain sim sync tables plugin test bench cachetrace preview screens demo dist clean distclean

all: plugin

# ---- setup -------------------------------------------------------------------------------------------------------
setup: venv toolchain

venv: $(VENV)/.done
$(VENV)/.done:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install -q numpy scipy pyelftools pycairo
	touch $@

# the SDK's cross compiler (tools/Dockerfile)
toolchain:
	@docker image inspect $(DOCKER_IMAGE) >/dev/null 2>&1 && echo "$(DOCKER_IMAGE): already built" || \
	  docker build --target toolchain -t $(DOCKER_IMAGE) -f tools/Dockerfile tools

# Blackfin instruction-set simulator (gdb's sim), for `make bench`
sim: toolchain
	@docker image inspect $(SIM_IMAGE) >/dev/null 2>&1 && echo "$(SIM_IMAGE): already built" || \
	  docker build --target sim -t $(SIM_IMAGE) -f tools/Dockerfile tools

# ---- build -------------------------------------------------------------------------------------------------------
sync: venv
	@git submodule status op1r-src | grep -q '^-' && git submodule update --init op1r-src || true
	@mkdir -p $(DST)
	@rsync -a --delete --exclude test/build --exclude __pycache__ plugin/ $(DST)/
	@$(PY) -c "import json,sys; p='$(DST)/plugin.json'; j=json.load(open(p)); j['name']='$(NAME)'; \
	  json.dump(j, open(p,'w'), indent=2)"

tables: venv
	cd plugin && $(PY) gen_tables.py && $(PY) gen_svg.py && $(PY) gen_presets.py

plugin: venv sync
	cd $(SDK) && $(PY) make_plugin.py plugins/moai
	@mkdir -p build && cp $(SDK)/build/plugins/$(NAME).op1p build/
	@echo "-> build/$(NAME).op1p"

test: venv sync
	cd $(DST)/test && ./run.sh
	cd $(DST)/test && $(PY) levels.py && $(PY) filter_response.py && $(PY) ring_check.py && \
	  $(PY) osc_check.py && $(PY) env_check.py && $(PY) cpu_guard.py && $(PY) fuzz.py && $(PY) preset_levels.py && build/mm_ui && build/mm_scope

bench: sync
	tools/bench.sh $(DST)

cachetrace: sync
	tools/cachetrace.sh $(DST)

preview: venv sync
	@mkdir -p build
	cd $(DST)/test && ./run.sh >/dev/null && $(PY) preview.py $(abspath build)/preview.png

screens: venv sync
	cd $(DST)/test && ./run.sh >/dev/null && $(PY) preview.py --screens $(abspath docs/screens)

demo: venv sync
	@mkdir -p build
	cd $(DST)/test && ./run.sh >/dev/null && $(PY) demo.py $(abspath build)/demo.wav

dist: plugin preview demo

clean:
	rm -rf build $(DST) $(SDK)/build/plugins/moai $(SDK)/build/plugins/$(NAME).*

distclean: clean
	rm -rf $(VENV)
