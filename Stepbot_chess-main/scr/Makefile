# Makefile for Stepbot
# Build the engine with: make
# Clean up with:         make clean
#
# Requires g++ with C++17 support.
# On Windows, use the MSYS2 MinGW x64 terminal.

# ─────────────────────────────────────────
# COMPILER SETTINGS
# ─────────────────────────────────────────

# CXX is the C++ compiler — g++ is the standard choice
CXX = g++

# CXXFLAGS are compiler options:
#   -O2         = optimise for speed (important for a chess engine)
#   -std=c++17  = use C++17 standard (needed for structured bindings)
#   -Wall       = show all warnings (helpful when developing)
#   -Wextra     = show extra warnings
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -pthread

# On Windows we want a static binary so it runs without extra DLLs
# Detect Windows by checking if the shell is cmd/powershell
ifeq ($(OS), Windows_NT)
    TARGET   = stepbot.exe
    TEST_TARGET = smoke_tests.exe
    BENCH_TARGET = bench_tests.exe
    LDFLAGS  = -static -lkernel32 -lpthread
else
    TARGET   = stepbot
    TEST_TARGET = smoke_tests
    BENCH_TARGET = bench_tests
    LDFLAGS  = -lpthread
endif

# ─────────────────────────────────────────
# SOURCE FILES
# All .cpp files that make up the engine.
# Add new .cpp files here if you add modules.
# ─────────────────────────────────────────

SRCS = main.cpp \
       board.cpp \
       movegen.cpp \
       evaluate.cpp \
       zobrist.cpp \
       search.cpp

# Object files — one .o per .cpp
# This line transforms each "foo.cpp" in SRCS into "foo.o"
OBJS = $(SRCS:.cpp=.o)
TEST_SRCS = smoke_tests.cpp \
            board.cpp \
            movegen.cpp \
            evaluate.cpp \
            zobrist.cpp \
            search.cpp

BENCH_SRCS = bench_tests.cpp \
             board.cpp \
             movegen.cpp \
             evaluate.cpp \
             zobrist.cpp \
             search.cpp

# ─────────────────────────────────────────
# TARGETS
# ─────────────────────────────────────────

# Default target — runs when you just type 'make'
# Builds the executable from all object files
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Run with: ./$(TARGET)"
	@echo ""

# Pattern rule: compile any .cpp into a .o
# $< = the .cpp file, $@ = the .o file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up compiled files
clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) $(BENCH_TARGET)
	@echo "  Cleaned."

# Rebuild from scratch
rebuild: clean all

# Run the engine directly (useful for quick testing)
run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_SRCS) $(LDFLAGS)

bench: $(BENCH_TARGET)
	./$(BENCH_TARGET)

bench-multipv: $(BENCH_TARGET)
	./$(BENCH_TARGET) --multipv

bench-timed: $(BENCH_TARGET)
	./$(BENCH_TARGET) --timed --runs 1

$(BENCH_TARGET): $(BENCH_SRCS)
	$(CXX) $(CXXFLAGS) -o $(BENCH_TARGET) $(BENCH_SRCS) $(LDFLAGS)

# Declare targets that aren't files
.PHONY: all clean rebuild run test bench bench-multipv bench-timed
