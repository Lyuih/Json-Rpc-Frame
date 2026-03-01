# 编译器和基础编译选项
CXX := g++
CXXFLAGS := -std=c++17 -Wall -g


# ===================== 路径配置 =====================
# 1. 头文件搜索路径
INCLUDES := -I/home/yui/code/build/release-install-cpp11/include
# 2. 库文件搜索路径
LIBDIRS := -L/home/yui/code/build/release-install-cpp11/lib

# ===================== 链接库配置 =====================
# 基础库 + muduo库（静态库链接方式和动态库一致，-l参数通用）
LIBS := -lspdlog -lfmt -ljsoncpp -lgtest -lgtest_main -lpthread -lmuduo_net -lmuduo_base

# ===================== 自动推导文件 =====================
# 查找当前目录下所有.cc文件
SRCS := $(wildcard *.cc)
# 将.cc文件转换为同名可执行文件
TARGETS := $(patsubst %.cc, %, $(SRCS))

# ===================== 构建规则 =====================
# 伪目标：默认/清理
.PHONY: all clean

# 默认目标：编译所有可执行文件
all: $(TARGETS)
	@echo "✅ 所有文件编译完成！生成的可执行文件：$(TARGETS)"

# 通用规则：所有.cc文件编译为同名可执行文件（包含正确的muduo路径）
%: %.cc
	$(CXX) -o $@ $< $(CXXFLAGS) $(INCLUDES) $(LIBDIRS) $(LIBS)
	@echo "🔨 编译完成：$@"

# 清理规则：删除所有生成的可执行文件
clean:
	rm -rf $(TARGETS)
	@echo "🗑️  清理完成！已删除：$(TARGETS)"