# Directories

SOURCE_DIR = src/source
OBJ_DIR_SOURCE = obj/source

DESTINATION_DIR = src/destination
OBJ_DIR_DESTINATION = obj/destination

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TOOL_DIR = tools
ARCH_DIR = dist
TIME_DIR = time
TEST_DIR = test

# Programs

EXECUTABLE_NAME_SRC = source
EXECUTABLE_SRC = $(BIN_DIR)/./$(EXECUTABLE_NAME_SRC)

EXECUTABLE_NAME_DST = destination
EXECUTABLE_DST = $(BIN_DIR)/./$(EXECUTABLE_NAME_DST)

# Compiler

CC = gcc
CFLAGS = -Wall -Wextra -Wvla -Werror -g

# Files and folders

SRCS_SOURCE = $(shell find $(SOURCE_DIR) -name '*.c')
SRC_DIRS_SOURCE = $(shell find $(SOURCE_DIR) -type d | sed 's@$(SOURCE_DIR)@.@g' )
OBJS_SOURCE = $(patsubst $(SOURCE_DIR)/%.c,$(OBJ_DIR_SOURCE)/%.o,$(SRCS_SOURCE))

SRCS_DESTINATION = $(shell find $(DESTINATION_DIR) -name '*.c')
SRC_DIRS_DESTINATION = $(shell find $(DESTINATION_DIR) -type d | sed 's@$(DESTINATION_DIR)@.@g' )
OBJS_DESTINATION = $(patsubst $(DESTINATION_DIR)/%.c,$(OBJ_DIR_DESTINATION)/%.o,$(SRCS_DESTINATION))

# Compiling

all : build_dir_source build_dir_destination title $(BIN_DIR)/$(EXECUTABLE_NAME_SRC) $(BIN_DIR)/$(EXECUTABLE_NAME_DST)

$(BIN_DIR)/$(EXECUTABLE_NAME_SRC) : build_dir_source $(OBJS_SOURCE)
	@echo "\n> Compiling source : "
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS_SOURCE) -o $@

$(BIN_DIR)/$(EXECUTABLE_NAME_DST) : build_dir_destination $(OBJS_DESTINATION)
	@echo "\n> Compiling destination: "
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS_DESTINATION) -o $@

$(OBJ_DIR_SOURCE)/%.o: $(SOURCE_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR_DESTINATION)/%.o: $(DESTINATION_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run

source_saw: title_src
	@./bin/source "stop and wait" "127.0.0.1" 3333 4444

source_gbn: title_src
	@./bin/source "go-back-n" "127.0.0.1" 3333 4444

destination: title_dst
	@./bin/destination "127.0.0.1" 6666 5555

medium: title_med
	@python3 tools/medium/medium.py -v -s
	# @python3 tools/medium/medium.py -v -s -e -l 100

# Utils

build_dir_source:
	@$(call make-obj-src)

build_dir_destination:
	@$(call make-obj-dst)

clean:
	@echo "> Cleaning :"
	rm -rf $(OBJ_DIR)
	rm -rf $(BIN_DIR)
	rm -rf $(ARCH_DIR)

dist: clean
	@mkdir -p $(ARCH_DIR)
	@echo "> Archiving :"
	tar -czvf $(ARCH_DIR)/AlgoReseaux_Projet.tar.gz Makefile README.md $(TOOL_DIR) $(SRC_DIR) $(TIME_DIR) $(TEST_DIR)

report:
	@echo "> Generating report:"
	@(cd doc; ./generate_report.sh; cd ..)

# Functions

# Create obj directory structure

define make-obj-src
	mkdir -p $(OBJ_DIR_SOURCE)
	for dir in $(SRC_DIRS_SOURCE); \
	do \
		mkdir -p $(OBJ_DIR_SOURCE)/$$dir; \
	done
endef

define make-obj-dst
	mkdir -p $(OBJ_DIR_DESTINATION)
	for dir in $(SRC_DIRS_DESTINATION); \
	do \
		mkdir -p $(OBJ_DIR_DESTINATION)/$$dir; \
	done
endef

# Others

help:
	@echo "> List of commands :"
	@echo "make -> compiles the programs"
	@echo "make medium -> runs the medium, default : \n\t -v -s -e -l 100"
	@echo "make destination -> runs the destination, default : \n\t 'localhost' 6666 5555"
	@echo "make source_saw -> runs the source, default : \n\t 'stop and wait' 'localhost' 3333 4444"
	@echo "make source_gbn -> runs the source, default : \n\t 'go-back-n' 'localhost' 3333 4444"
	@echo "make clean -> clears the directory"
	@echo "make dist -> creates an archive"
	@echo "make report -> creates the report"
	@echo "make help -> to display all informations"

# Titles

title:
	@echo "  _______ _____ _____              _               _    _ _____  _____  "
	@echo " |__   __/ ____|  __ \            (_)             | |  | |  __ \|  __ \ "
	@echo "    | | | |    | |__) |  _   _ ___ _ _ __   __ _  | |  | | |  | | |__) |"
	@echo "    | | | |    |  ___/  | | | / __| |  _ \ / _  | | |  | | |  | |  ___/ "
	@echo "    | | | |____| |      | |_| \__ \ | | | | (_| | | |__| | |__| | |     "
	@echo "    |_|  \_____|_|       \__,_|___/_|_| |_|\__, |  \____/|_____/|_|     "
	@echo "                                            __/ |                       "
	@echo "                                           |___/                        "
	@echo "> Help : type <make help> to check out the list of commands\n"

title_med:
	@echo "                    _ _               "
	@echo "                   | (_)              "
	@echo " _ __ ___   ___  __| |_ _   _ _ __ ___"
	@echo "|  _   _ \ / _ \/ _  | | | | |  _   _ \ "
	@echo "| | | | | |  __/ (_| | | |_| | | | | | |"
	@echo "|_| |_| |_|\___|\__,_|_|\__,_|_| |_| |_|"

title_src:
	@echo "  ___  ___  _   _ _ __ ___ ___ "
	@echo " / __|/ _ \| | | | '__/ __/ _ \ "
	@echo " \__ \ (_) | |_| | | | (_|  __/"
	@echo " |___/\___/ \__,_|_|  \___\___|"

title_dst:
	@echo "      _           _   _             _   _             "
	@echo "     | |         | | (_)           | | (_)            "
	@echo "   __| | ___  ___| |_ _ _ __   __ _| |_ _  ___  _ __  "
	@echo "  / _  |/ _ \/ __| __| |  _ \ / _  | __| |/ _ \|  _ \ "
	@echo " | (_| |  __/\__ \ |_| | | | | (_| | |_| | (_) | | | |"
	@echo "  \__,_|\___||___/\__|_|_| |_|\__,_|\__|_|\___/|_| |_|"