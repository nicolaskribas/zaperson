SRC_DIR = ./src
BUILD_DIR = ./build
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
MKDIR_P = mkdir -p
RM = rm -f 
TARGET = zaperson
CC = gcc
CFLAG = -Wall 
CLIBS = -lpaho-mqtt3as -lncurses

$(TARGET): $(OBJ)
	$(CC) $(CFLAG) $^ -o $@ $(CLIBS) 

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAG) -c $^ -o $@ $(CLIBS)

.PHONY: run clean 
	
run: $(TARGET) 
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJ) 

