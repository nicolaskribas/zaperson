SRC_DIR 	= ./src
BUILD_DIR 	= ./build

SRC 	= $(wildcard $(SRC_DIR)/*.c)
OBJ 	= $(SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET 	= zaperson
CC 		= gcc
CFLAG 	= -Wall -Wextra
MKDIR_P = mkdir -p
RM 		= rm -f 

$(TARGET): $(OBJ)
	$(CC) $(CFLAG) $^ -o $@ 

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAG) -c $^ -o $@

.PHONY: run clean 
	
run: $(TARGET) 
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJ) 
