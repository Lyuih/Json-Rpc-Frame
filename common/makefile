CXX := g++
CXXFLAGS := -std=c++17 -Wall -g
LDFLAGS := -lspdlog -lfmt -ljsoncpp -lgtest -lgtest_main -lpthread

# 1. 查找当前目录下所有.cc文件（如test1.cc、test2.cc）
SRCS := $(wildcard *.cc)
# 2. 将.cc文件替换为对应的可执行文件名（如test1.cc → test1）
TARGETS := $(patsubst %.cc, %, $(SRCS))

# ===================== 构建规则 =====================
# 伪目标：默认目标（执行make时编译所有可执行文件）
.PHONY: all clean

# 默认目标：编译所有可执行文件
all: $(TARGETS)
	@echo "✅ 所有文件编译完成！生成的可执行文件：$(TARGETS)"

# 核心模式规则：匹配任意可执行文件（%），依赖对应的.cc文件（%.cc）
# 作用：每个.cc文件独立编译成同名可执行文件
%: %.cc
	$(CXX) -o $@ $< $(CXXFLAGS) $(LDFLAGS)
	@echo "🔨 编译完成：$@"

# 清理规则：删除所有生成的可执行文件
clean:
	rm -rf $(TARGETS)
	@echo "🗑️  清理完成！已删除：$(TARGETS)"