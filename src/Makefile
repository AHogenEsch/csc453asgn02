CC          = gcc
CFLAGS      = -Wall -g -fPIC -std=gnu99 -I.
LDFLAGS     = -shared
TARGET      = liblwp.so

# Source and Header Files
C_SOURCES   = lwp.c
AS_SOURCES  = magic64.S 
HEADER      = lwp.h

# Object files needed to link the library
OBJECTS     = $(C_SOURCES:.c=.o) $(AS_SOURCES:.S=.o)

all: liblwp.so

# --- Primary Target: liblwp.so ---
# Link all object files into the shared library
# $@ expands to the target, and $< to the first dependency
liblwp.so: lwp.o magic64.o
	$(CC) $(LDFLAGS) -o $@ lwp.o magic64.o
	@echo "Successfully built liblwp.so"

lwp.o: lwp.c lwp.h
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly Rule: Compiles .S files to .o files
magic64.o: magic64.S
	$(CC) -c $< -o $@

# --- Cleanup Target ---
.PHONY: clean
clean:
	rm -f lwp.o magic64.o liblwp.so
	@echo "Cleaned build files."