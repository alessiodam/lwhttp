CE_LIBIFY ?= ce-libify
CONVBIN ?= convbin
HOST_CC ?= cc
HOST_WARNINGS := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror
HOST_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_CC ?= clang
FUZZ_SECONDS ?= 60
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck
GCOVR ?= gcovr
PYTHON ?= python3
SPHINX_BUILD ?= sphinx-build
LIB_BUDGET ?= 32768
ABI_BASE ?= $(shell git describe --tags --abbrev=0 --match 'v*' 2>/dev/null)
CEMU_AUTOTESTER ?= cemu-autotester
AUTOTESTER_ROM ?=
LWIP_DIST ?= ../lwip-ce/build
CLIBS ?= clibs.8xg
LWIP_INSTALL_CRC ?= F7C03D8D
HTTPTEST_VERDICT_CRC ?= 46258A1A
VERSION := $(shell sed -n 's/^\#define LWHTTP_VERSION_STRING "\(.*\)"/\1/p' lwhttp.h)
CORE := src/parser.c src/headers.c src/writer.c src/url.c src/util.c
SHARED_TESTS := tests/device/src/check.c tests/device/src/unit.c
HOST_SOURCES := $(CORE) $(SHARED_TESTS) tests/host/main.c
FUZZERS := parser url misc
FORMATTED := lwhttp.h $(wildcard src/*.c src/*.h tests/host/*.c tests/fuzz/*.c tests/device/src/*.c tests/device/src/*.h examples/*/src/*.c)
EXAMPLES := $(wildcard examples/*/makefile)
CI_FILES := tests/device/ci

.PHONY: all build test docs docs-check coverage fuzz $(FUZZERS:%=fuzz-%) lint format exports abi-check size-check install examples httptest \
	emulator-test package clean version

all: build

build:
	$(CE_LIBIFY) build

test:
	mkdir -p obj/host
	$(HOST_CC) $(HOST_WARNINGS) $(HOST_CFLAGS) -I . -o obj/host/test_lwhttp $(HOST_SOURCES)
	./obj/host/test_lwhttp

docs: docs-check
	$(SPHINX_BUILD) -W --keep-going -b html docs obj/docs/html

docs-check:
	$(PYTHON) -I docs/check_exports.py

coverage:
	rm -rf obj/coverage
	mkdir -p obj/coverage
	$(HOST_CC) $(HOST_WARNINGS) -O0 -g --coverage -I . -o obj/coverage/test_lwhttp $(HOST_SOURCES)
	./obj/coverage/test_lwhttp
	$(GCOVR) --root . --filter src/ --object-directory obj/coverage --print-summary --txt obj/coverage/coverage.txt \
		--markdown obj/coverage/coverage.md --html-details obj/coverage/index.html

fuzz: $(FUZZERS:%=fuzz-%)

$(FUZZERS:%=fuzz-%): fuzz-%: obj/fuzz/fuzz_%
	mkdir -p obj/fuzz/corpus/$*
	./obj/fuzz/fuzz_$* -max_total_time=$(FUZZ_SECONDS) -print_final_stats=1 -artifact_prefix=obj/fuzz/$*- \
		obj/fuzz/corpus/$* tests/fuzz/corpus/$*

obj/fuzz/fuzz_%: tests/fuzz/fuzz_%.c $(CORE) lwhttp.h src/internal.h
	mkdir -p $(@D)
	$(FUZZ_CC) -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -I . -o $@ $< $(CORE)

lint:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMATTED)
	$(CPPCHECK) --error-exitcode=1 --enable=warning,performance,portability --std=c11 --quiet \
		--suppress=missingIncludeSystem -I . $(HOST_SOURCES) tests/fuzz/*.c
	$(PYTHON) -m py_compile tests/device/autotest.py tests/device/httplog.py docs/conf.py docs/check_exports.py \
		.github/scripts/semver.py
	$(PYTHON) -I .github/scripts/semver.py --self-test
	$(PYTHON) -I .github/scripts/semver.py --check-header

format:
	$(CLANG_FORMAT) -i $(FORMATTED)

exports:
	$(CE_LIBIFY) exports

abi-check:
	@if [ -z "$(ABI_BASE)" ]; then \
		echo "abi-check: no release tag to compare against"; \
	else \
		mkdir -p obj && git show $(ABI_BASE):exports.txt > obj/abi-base.txt && \
		if head -n $$(wc -l < obj/abi-base.txt) exports.txt | cmp -s - obj/abi-base.txt; then \
			echo "abi-check: exports.txt is append-only relative to $(ABI_BASE)"; \
		else \
			echo "abi-check: exports.txt reorders or removes exports released in $(ABI_BASE)"; \
			diff obj/abi-base.txt exports.txt; \
			exit 1; \
		fi; \
	fi

size-check: build
	@size=$$(wc -c < bin/LWHTTP.8xv); \
	echo "size-check: LWHTTP.8xv is $$size bytes, budget $(LIB_BUDGET)"; \
	[ $$size -le $(LIB_BUDGET) ]

install: build
	$(CE_LIBIFY) install

examples: install
	$(foreach m,$(EXAMPLES),$(MAKE) -C $(dir $(m)) &&) true

httptest: install
	$(MAKE) -C tests/device

emulator-test: httptest
	rm -rf $(CI_FILES) tests/device/HTTPLOG.8xv
	mkdir -p $(CI_FILES) obj
	cp $(LWIP_DIST)/appinst/*.8x* $(LWIP_DIST)/lwip.8xv bin/LWHTTP.8xv $(CLIBS) $(CI_FILES)/
	printf 1 > obj/httpci.bin
	$(CONVBIN) -i obj/httpci.bin -o $(CI_FILES)/HTTPCI.8xv -j bin -k 8xv -n HTTPCI
	$(PYTHON) -I tests/device/autotest.py --files $(CI_FILES) --install-crc $(LWIP_INSTALL_CRC) \
		--verdict-crc $(HTTPTEST_VERDICT_CRC) --save-var --out tests/device/autotest.json
	cd tests/device && AUTOTESTER_ROM="$(AUTOTESTER_ROM)" $(CEMU_AUTOTESTER) "$$PWD/autotest.json"; status=$$?; \
		$(PYTHON) -I httplog.py HTTPLOG.8xv || status=1; exit $$status

package: build
	$(CE_LIBIFY) package --release $(VERSION)

version:
	@echo $(VERSION)

clean:
	$(CE_LIBIFY) clean
	$(foreach m,$(EXAMPLES),$(MAKE) -C $(dir $(m)) clean &&) true
	$(MAKE) -C tests/device clean
	rm -rf $(CI_FILES) tests/device/autotest.json tests/device/HTTPLOG.8xv
