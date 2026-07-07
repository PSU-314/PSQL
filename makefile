CXX := g++
CXXFLAGS := -g -std=c++17 -Wall -Wextra -Iinclude
AR := ar
ARFLAGS := rcs

SRC_DIR := src
OBJ_DIR := obj
BIN_NAME := psql
LIB_NAME := libpsql.a
DATA_DIR := data

# Find all source files and map them to object files
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Filter out cli.o
LIB_OBJS := $(filter-out $(OBJ_DIR)/cli.o, $(OBJS))

# Default target now builds both the executable and the static library
all: $(BIN_NAME) $(LIB_NAME)

# 1. Build the standalone executable (includes cli.o)
$(BIN_NAME): $(OBJS)
	@echo "Linking $@..."
	@$(CXX) $(OBJS) -o $@
	@echo "Executable build successful!"

# 2. Build the static library (excludes cli.o)
$(LIB_NAME): $(LIB_OBJS)
	@echo "Archiving static library $@..."
	@$(AR) $(ARFLAGS) $@ $^
	@echo "Static library build successful!"

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJ_DIR) $(BIN_NAME) $(LIB_NAME) $(wildcard *.db) $(DATA_DIR)
	@echo "Clean complete."

.PHONY: all clean