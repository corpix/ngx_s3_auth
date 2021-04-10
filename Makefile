CC ?= gcc

.DEFAULT_GOAL := all

tmux         := tmux -2 -f $(root)/.tmux.conf -S .tmux
tmux_session := $(name)

, = ,

define fail
{ echo "error: "$(1) 1>&2; exit 1; }
endef

## targets

.PHONY: all
all: # test, check and build all cmds

build: # build nginx with module attached with nix
	nix-build nix/nginx.nix

.PHONY: test
test: # compile & run tests
	gcc ./ngx_http_s3_auth_test.c $(CFLAGS) -o ./$@
	./$@


.PHONY: help
help: # print defined targets and their comments
	@grep -Po '^[a-zA-Z%_/\-\s]+:+(\s.*$$|$$)' Makefile \
		| sort                                      \
		| sed 's|:.*#|#|;s|#\s*|#|'                 \
		| column -t -s '#' -o ' | '

.PHONY: run/nginx
run/nginx: log run cache # run nginx
	nginx -g "daemon off;" -c nginx.conf -p $(root)

.PHONY: run/valgrind/nginx
run/valgrind/nginx: log run cache # run nginx with valgrind
	valgrind --trace-children=yes --log-file=log/memcheck.log --tool=memcheck \
		nginx -g "daemon off;" -c nginx.conf -p $(root)

.PHONY: run/nginx/reload
run/nginx/reload: # reload running nginx
	kill -SIGHUP $(shell cat run/nginx.pid)

.PHONY: run/nginx/test
run/nginx/test: # test nginx configuration
	nginx -t -c nginx.conf -p $(root)

clean:: # remove nginx data
	rm -rf cache run log

log run: # create diretories required to run nginx
	mkdir -p $@
cache: # create cache directories for nginx
	mkdir -p $@/client_body $@/fastcgi $@/proxy $@/scgi $@/uwsgi

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

test/minio/data: # make sure minio data directory exists
	mkdir -p $@

.PHONY: run/minio
run/minio: test/minio/data # run minio metrics collection service
	@bash -xec "cd $(dir $<); exec minio server --address 127.0.0.1:9000 ./data"

.PHONY: run/minio/init
run/minio/init: test/minio # initialize a running minio s3 with test data
	s3cmd -c $(root)/.s3cfg mb s3://cdn || true
	wget --directory-prefix=$< --mirror https://corpix.dev
	s3cmd -c $(root)/.s3cfg sync --acl-public $</corpix.dev s3://cdn

clean:: # remove minio data
	rm -rf test/minio/data
	rm -rf test/minio/corpix.dev

.PHONY: clean
clean:: # clean stored state
	rm -rf result*
	rm -rf build
