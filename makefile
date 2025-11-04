# Beagle Smalltalk 
#    Copyright 2025 Simberon Incorporated

ifeq ($(OS),Windows_NT)
	CC = gcc -c -m64 -g -DWINDOWS -D_GNU_SOURCE -O3
	EXE=beagle.exe
else
	CC = gcc -c -m64 -g -D_GNU_SOURCE
	EXE=beagle
endif
LN = gcc -m64 -g -O3
SRC = src
OBJ = obj

all:	$(EXE)

clean:
	rm -f $(OBJ)/socket_primitives.o $(OBJ)/file_primitives.o $(OBJ)/integer_primitives.o $(OBJ)/float_primitives.o $(OBJ)/primitive.o $(OBJ)/image.o $(OBJ)/memory_primitives.o
	rm -f $(OBJ)/interpret.o $(OBJ)/memory.o $(OBJ)/utility.o $(OBJ)/remote.o $(OBJ)/WinMain.o beagle.exe
	rm -f $(OBJ)/error.log $(OBJ)/websockets.o

dir_guard=@mkdir -p $(@D)

$(OBJ)/socket_primitives.o: $(SRC)/socket_primitives.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/socket_primitives.c -o $(OBJ)/socket_primitives.o

$(OBJ)/file_primitives.o: $(SRC)/file_primitives.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/file_primitives.c -o $(OBJ)/file_primitives.o

$(OBJ)/integer_primitives.o: $(SRC)/integer_primitives.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/integer_primitives.c -o $(OBJ)/integer_primitives.o

$(OBJ)/float_primitives.o: $(SRC)/float_primitives.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/float_primitives.c -o $(OBJ)/float_primitives.o

$(OBJ)/primitive.o: $(SRC)/primitive.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/primitive.c -o $(OBJ)/primitive.o

$(OBJ)/image.o: $(SRC)/image.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/image.c -o $(OBJ)/image.o

$(OBJ)/interpret.o: $(SRC)/interpret.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/interpret.c -o $(OBJ)/interpret.o

$(OBJ)/memory.o: $(SRC)/memory.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/memory.c -o $(OBJ)/memory.o

$(OBJ)/utility.o: $(SRC)/utility.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/utility.c -o $(OBJ)/utility.o

$(OBJ)/remote.o: $(SRC)/remote.c $(SRC)/object.h $(SRC)/websockets.h
	$(dir_guard)
	$(CC) $(SRC)/remote.c -o $(OBJ)/remote.o

$(OBJ)/websockets.o: $(SRC)/websockets.c $(SRC)/object.h $(SRC)/websockets.h
	$(dir_guard)
	$(CC) $(SRC)/websockets.c -o $(OBJ)/websockets.o

$(OBJ)/memory_primitives.o: $(SRC)/memory_primitives.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/memory_primitives.c -o $(OBJ)/memory_primitives.o

$(OBJ)/WinMain.o: $(SRC)/WinMain.c $(SRC)/object.h
	$(dir_guard)
	$(CC) $(SRC)/WinMain.c -o $(OBJ)/WinMain.o

$(EXE): $(OBJ)/image.o $(OBJ)/memory.o $(OBJ)/interpret.o $(OBJ)/utility.o $(OBJ)/remote.o $(OBJ)/WinMain.o\
	$(OBJ)/socket_primitives.o $(OBJ)/file_primitives.o $(OBJ)/integer_primitives.o $(OBJ)/float_primitives.o $(OBJ)/primitive.o $(OBJ)/websockets.o $(OBJ)/memory_primitives.o
	$(LN)	$(OBJ)/image.o $(OBJ)/memory.o $(OBJ)/interpret.o $(OBJ)/utility.o $(OBJ)/remote.o $(OBJ)/WinMain.o \
	$(OBJ)/socket_primitives.o $(OBJ)/file_primitives.o $(OBJ)/integer_primitives.o $(OBJ)/float_primitives.o $(OBJ)/primitive.o $(OBJ)/memory_primitives.o \
	$(OBJ)/websockets.o -lm -o $(EXE)

