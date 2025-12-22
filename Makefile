CFLAGS = -std=c89 -Wall -Wextra
SRCS = src/hash-table.c src/lcl-api.c src/lcl-cell.c src/lcl-command.c \
       src/lcl-dict.c src/lcl-env.c src/lcl-eval.c src/lcl-frame.c \
       src/lcl-interp.c src/lcl-list.c src/lcl-ns.c src/lcl-num.c \
       src/lcl-proc.c src/lcl-program.c src/lcl-ref.c src/lcl-scan.c \
       src/lcl-stdlib.c src/lcl-str.c src/lcl-string.c src/lcl-word.c \
       src/str-compat.c

lcl: $(SRCS) src/lcl-main.c
	gcc $(CFLAGS) -O2 -o lcl $(SRCS) src/lcl-main.c

debug: $(SRCS) src/lcl-main.c
	gcc $(CFLAGS) -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o lcl $(SRCS) src/lcl-main.c

test: $(SRCS) src/lcl-test.c
	gcc $(CFLAGS) -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DLCL_TEST -DDEBUG_REFC -o lcl-test $(SRCS) src/lcl-test.c

liblcl.so: $(SRCS)
	gcc $(CFLAGS) -O2 -fPIC -shared -Iinclude -o liblcl.so $(SRCS)

