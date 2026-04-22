# Paths are relative to this directory (the crossink-reader repo root).
PIO ?= ../.venv/bin/pio
ENV_SIMULATOR ?= simulator
ENV_FIRMWARE ?= no_emoji

.PHONY: help simulator upload build

help:
	@echo "Targets:"
	@echo "  simulator  Build and run the desktop SDL simulator ($(ENV_SIMULATOR), target run_simulator)"
	@echo "  upload     Build and deploy firmware ($(ENV_FIRMWARE))"
	@echo "  build      Build firmware only ($(ENV_FIRMWARE), no upload)"
	@echo ""
	@echo "Override: make PIO=/path/to/pio upload"

simulate:
	$(PIO) run -e $(ENV_SIMULATOR) && .pio/build/simulator/program

upload:
	$(PIO) run -e $(ENV_FIRMWARE) -t upload

build:
	$(PIO) run -e $(ENV_FIRMWARE)
