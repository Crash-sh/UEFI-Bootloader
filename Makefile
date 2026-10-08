.DEFAULT_GOAL := all

.PHONY: test-matrix test-production
test-matrix test-production:
	$(MAKE) -C "$(ROOT)/src" $@

.PHONY: update-linux
update-linux:
	$(MAKE) -C "$(ROOT)/src" $@

.PHONY: test-secureboot sign package-signed
test-secureboot sign package-signed:
	$(MAKE) -C "$(ROOT)/src" $@

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all check run clean check-deps test package stage-uki stage-linux tools check-tools check-linux test-linux

all check run clean check-deps test package stage-uki stage-linux tools check-tools check-linux test-linux:
	$(MAKE) -C "$(ROOT)/src" $@
