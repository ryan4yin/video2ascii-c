# 参考：https://github.com/shouxieai/makefile_tutorial_project/blob/main/Makefile
CC=clang
LIBAV_LIBS=libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample ncurses

# compile flags
CFLAGS         := -std=c11 -Wall -O3 
# add include paths into compile flags
CFLAGS         += $(shell pkg-config --cflags $(LIBAV_LIBS))

# link flags
LFLAGS         += $(shell pkg-config --libs $(LIBAV_LIBS))

TARGET         := bin/video2ascii
TARGET_DIR     := $(dir $(TARGET))

# 找到所有的 src/**/*.c 源文件
srcs := $(shell find src -name "*.c")
# 将文件名的后缀替换为 .o
objs := $(srcs:.c=.o)
# 将 .o 文件的文件夹改为 objs/ 得到最终的 objects 文件地址
objs := $(objs:src/%=objs/%)

# 编译所有 .c 文件，生成 .o 中间文件
# -c 表示生成 .o 中间文件
# $< 是每一个依赖项的名称，即   `src/xxx.c`
# $@ 是 step 的名称，即       `objs/xxx.o`
objs/%.o : src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

# 链接所有中间文件以及其他依赖库，生成可执行文件
# $^ 是所有依赖项的名称，即 `objs/xxx.o objs/yyy.o ...`
build: $(objs)
	mkdir -p $(TARGET_DIR)
	$(CC) $^ -o $(TARGET) $(LFLAGS) -lz

run: build
	./bin/video2ascii BadApple.mp4 output.mp4

clean:
	rm $(TARGET) $(objs)
