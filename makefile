CXX := g++
CXXFLAGS := -g -std=c++17 -Wall -Wextra -Iinclude

SRC_DIR := src
OBJ_DIR := obj
BIN_NAME := psql
DATA_DIR := data

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(BIN_NAME)

$(BIN_NAME): $(OBJS)
	@echo "Linking $@..."
	@$(CXX) $(OBJS) -o $@
	@echo "Build successful!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@


$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJ_DIR) $(BIN_NAME) $(wildcard *.db) $(DATA_DIR)
	@echo "Clean complete."

.PHONY: all clean