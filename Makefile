VERSION = 0.1.0

CC = clang
CPPFLAGS += -DAPP_VERSION='"$(VERSION)"'
CFLAGS = -std=c11 -Os -Wall -Wextra -Werror -mmacosx-version-min=13.0
LDLIBS = -framework ApplicationServices -framework CoreServices -framework CoreFoundation
BIN = build/finder-replace

.PHONY: all test version publish clean
all: $(BIN)

$(BIN): finder-replace.c Makefile
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) finder-replace.c $(LDLIBS) -o "$@"

test: $(BIN) build/test
	./build/test
	test "$$($(BIN) --version)" = "$(VERSION)"

build/test: test.c finder-replace.c Makefile
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) test.c $(LDLIBS) -o $@

version:
	@printf '%s\n' '$(VERSION)'

publish:
	git add -- Makefile
	git commit --only -m "chore: release v$(VERSION)" -- Makefile
	git tag "v$(VERSION)"
	git push --atomic origin HEAD "refs/tags/v$(VERSION)"

clean:
	rm -rf build
