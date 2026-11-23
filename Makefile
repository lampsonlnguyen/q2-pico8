# The Q2 PICO-8 player. Needs a FAKE-08 checkout (pinned revision in
# .github/workflows/fake08-port.yml); "player" also needs the MIPS cross
# toolchain and Q2 sysroot described in README.md.
FAKE08_SRC ?= ../fake-08

# dist/q2-pico8-playable-experimental.zip, the SD-card install
player:
	FAKE08_SRC=$(FAKE08_SRC) bash tools/build-fake08.sh

# Native checks: the Get Games helper, the full engine linked against the
# Q2 host layer, and the host Q2 simulation (library, controls, carts).
check:
	cd src/itch-helper && go vet -mod=vendor . && go test -mod=vendor .
	FAKE08_SRC=$(FAKE08_SRC) bash tools/test-fake08-link.sh
	FAKE08_SRC=$(FAKE08_SRC) bash tests/host_q2_sim/run.sh build/host-sim-report

clean:
	rm -rf build dist

.PHONY: player check clean
