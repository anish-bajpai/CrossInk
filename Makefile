# Paths are relative to this directory (the crossink-reader repo root).
PIO ?= ../.venv/bin/pio
ENV_SIMULATOR ?= simulator
ENV_FIRMWARE ?= no_emoji

.PHONY: help simulate copy-sentence-demo upload build

help:
	@echo "Targets:"
	@echo "  simulate           Build native simulator ($(ENV_SIMULATOR)) and run .pio/build/simulator/program"
	@echo "  copy-sentence-demo Copy data/sentence_demo.txt -> fs_/books/ (same layout as SD /books/)"
	@echo "  upload             Build and deploy firmware ($(ENV_FIRMWARE))"
	@echo "  build              Build firmware only ($(ENV_FIRMWARE), no upload)"
	@echo ""
	@echo "Override: make PIO=/path/to/pio upload"

copy-sentence-demo:
	@mkdir -p fs_/books
	@cp -f data/sentence_demo.txt fs_/books/sentence_demo.txt
	@echo "Installed fs_/books/sentence_demo.txt"

simulate:
	$(PIO) run -e $(ENV_SIMULATOR) && .pio/build/simulator/program

upload:
	$(PIO) run -e $(ENV_FIRMWARE) -t upload

build:
	$(PIO) run -e $(ENV_FIRMWARE)
