#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>

/*
    REQUIRES: *codebuffer is a valid pointer to 8080 assembly code,
	       pc is the current offset into the code
    EFFECTS:   returns the number of bytes of the opcode
 */

int disassemble_8080_op_code(unsigned char* codebuffer, int pc);
