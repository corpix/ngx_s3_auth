CC       ?= gcc
NGX_PATH ?= $(PWD)

cflags :=                              \
	-g                             \
	$(CFLAGS)                      \
	-I$(NGX_PATH)/src/os/unix      \
	-I$(NGX_PATH)/src/core         \
	-I$(NGX_PATH)/src/http         \
	-I$(NGX_PATH)/src/http/v2      \
	-I$(NGX_PATH)/src/http/modules \
	-I$(NGX_PATH)/src/event        \
	-I$(NGX_PATH)/objs/            \
	-I.

.DEFAULT_GOAL := all

tmux         := tmux -2 -f $(root)/.tmux.conf -S $(root)/.tmux
tmux_session := $(name)

, = ,

define fail
{ echo "error: "$(1) 1>&2; exit 1; }
endef

## targets

.PHONY: all
all: # test, check and build all cmds

.PHONY: test
test: # compile & run tests
	gcc ./ngx_http_s3_auth_test.c $(cflags) -o ./$@
	./$@


.PHONY: help
help: # print defined targets and their comments
	@grep -Po '^[a-zA-Z%_/\-\s]+:+(\s.*$$|$$)' Makefile \
		| sort                                      \
		| sed 's|:.*#|#|;s|#\s*|#|'                 \
		| column -t -s '#' -o ' | '

### releases

.PHONY: nix/repl
nix/repl: # start nix repl with current nixpkgs as a context
	nix repl '<nixpkgs>'

.PHONY: run/shell
run/shell: # enter development environment with nix-shell
	nix-shell

.PHONY: run/cage/shell
run/cage/shell: # enter sandboxed development environment with nix-cage
	nix-cage

.PHONY: run/tmux/session
run/tmux/session: # start development environment
	@$(tmux) has-session    -t $(tmux_session) && $(call fail,tmux session $(tmux_session) already exists$(,) use: '$(tmux) attach-session -t $(tmux_session)' to attach) || true
	@$(tmux) new-session    -s $(tmux_session) -n console -d
	@$(tmux) select-window  -t $(tmux_session):0

	@if [ -f $(root)/.personal.tmux.conf ]; then             \
		$(tmux) source-file $(root)/.personal.tmux.conf; \
	fi

	@$(tmux) attach-session -t $(tmux_session)

.PHONY: run/tmux/attach
run/tmux/attach: # attach to development session if running
	@$(tmux) attach-session -t $(tmux_session)

.PHONY: run/tmux/kill
run/tmux/kill: # kill development environment
	@$(tmux) kill-session -t $(tmux_session)

.PHONY: clean
clean:: # clean stored state
	rm -rf result*
	rm -rf build
