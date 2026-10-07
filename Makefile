.DEFAULT_GOAL := all

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all check run clean check-deps test package stage-uki tools check-tools

all check run clean check-deps test package stage-uki tools check-tools:
	$(MAKE) -C "$(ROOT)/src" $@
