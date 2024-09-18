#include <stdio.h>
#include "spimcore.h"

#define MEM_SIZE 65536
#define NUM_REGISTERS 32

// Control signals structure
struct controls {
    unsigned RegDst;
    unsigned Jump;
    unsigned Branch;
    unsigned MemRead;
    unsigned MemtoReg;
    unsigned ALUOp;
    unsigned MemWrite;
    unsigned ALUSrc;
    unsigned RegWrite;
};
void ALU(unsigned A, unsigned B, char ALUControl, unsigned *ALUresult, char *Zero);
int instruction_fetch(unsigned PC, unsigned *Mem, unsigned *instruction);
void instruction_partition(unsigned instruction, unsigned *op, unsigned *r1, unsigned *r2,
                           unsigned *r3, unsigned *funct, unsigned *offset, unsigned *jsec);
int instruction_decode(unsigned op, struct_controls *controls);
void read_register(unsigned r1, unsigned r2, unsigned *Reg, unsigned *data1, unsigned *data2);
void sign_extend(unsigned offset, unsigned *extended_value);
int ALU_operations(unsigned data1, unsigned data2, unsigned extended_value, unsigned funct,
                   char ALUOp, char ALUSrc, unsigned *ALUresult, char *Zero);
int rw_memory(unsigned ALUresult, unsigned data2, char MemWrite, char MemRead,
              unsigned *memdata, unsigned *Mem);
void write_register(unsigned r2, unsigned r3, unsigned memdata, unsigned ALUresult,
                    char RegWrite, char RegDst, char MemtoReg, unsigned *Reg);
void PC_update(unsigned jsec, unsigned extended_value, char Branch, char Jump, char Zero,
               unsigned *PC);



int main() {
    // Define memory, registers, and other variables
    unsigned Mem[MEM_SIZE];
    unsigned Reg[NUM_REGISTERS];
    unsigned PC = 0;

    // Main simulation loop
    while (1) {
        unsigned instruction, op, r1, r2, r3, funct, offset, jsec;
        unsigned data1, data2, extended_value, ALUresult, memdata;
        char Zero;
        struct controls controls;

        // Instruction fetch
        if (instruction_fetch(PC, Mem, &instruction))
            break; // Halt condition

        // Instruction partition
        instruction_partition(instruction, &op, &r1, &r2, &r3, &funct, &offset, &jsec);

        // Instruction decode
        if (instruction_decode(op, &controls))
            break; // Halt condition

        // Read register
        read_register(r1, r2, Reg, &data1, &data2);

        // Sign extend
        sign_extend(offset, &extended_value);

        // ALU operations
        ALU_operations(data1, data2, extended_value, funct, controls.ALUOp, controls.ALUSrc,
                       &ALUresult, &Zero);

        // Read/write memory
        if (rw_memory(ALUresult, data2, controls.MemWrite, controls.MemRead, &memdata, Mem))
            break; // Halt condition

        // Write register
        write_register(r2, r3, memdata, ALUresult, controls.RegWrite, controls.RegDst, controls.MemtoReg, Reg);

        // Update PC
        PC_update(jsec, extended_value, controls.Branch, controls.Jump, Zero, &PC);
    }

    // Print the final state of registers and memory
    printf("Final state of registers:\n");
    for (int i = 0; i < NUM_REGISTERS; i++) {
        printf("$%d: %u\n", i, Reg[i]);
    }

    printf("\nFinal state of memory:\n");
    for (int i = 0; i < MEM_SIZE; i += 4) {
        printf("0x%04X: %08X\n", i, Mem[i >> 2]);
    }

    return 0;
}

// ALU function
void ALU(unsigned A, unsigned B, char ALUControl, unsigned *ALUresult, char *Zero) {
    // Perform ALU operation based on ALUControl
    if(ALUControl == 0x0) {
        *ALUresult = A + B; // Addition
    }
    else if (ALUControl == 0x1) {
        *ALUresult = A - B; // Subtraction
    }
    else if (ALUControl == 0x2) {
        // Set ALUresult to 1 if A < B, otherwise set to 0
        int ones = 0b1111111111111111 << 16;
        int aExtended = (A&1<<15?A|ones:A);
        int bExtended = (B&1<<15?B|ones:B);
        *ALUresult = aExtended < bExtended;
    }
    else if (ALUControl == 0x3){
        // Set ALUresult to 1 if A < B (unsigned), otherwise set to 0
        *ALUresult = A < B;
    }
    else if (ALUControl == 0x4){
        *ALUresult = A & B; // Bitwise AND
    }
    else if (ALUControl == 0x5){
        *ALUresult = A | B; // Bitwise OR
    }
    else if (ALUControl == 0x6){
        *ALUresult = B << 16; // Shift left by 16 bits
    }
    else if (ALUControl == 0x7){
        *ALUresult = ~A; // Bitwise NOT
    }

    // Set Zero flag based on ALUresult
    if(*ALUresult == 0) *Zero = 1; else *Zero = 0;
}

// Instruction fetch
int instruction_fetch(unsigned PC, unsigned *Mem, unsigned *instruction) {
    unsigned i = PC >> 2;

    // Test for word alignment.
    if(PC % 4 != 0) return 1;

    // Get instruction from Memory then return 0;
    *instruction = Mem[i];
    return 0;
}

// Instruction partition
void instruction_partition(unsigned instruction, unsigned *op, unsigned *r1, unsigned *r2,
                           unsigned *r3, unsigned *funct, unsigned *offset, unsigned *jsec) {
    // Extract different fields of the instruction
    *op = instruction >> 26; // Opcode
    *r1 = (instruction & (0b11111 << 20)) >> 21; // Source register 1
    *r2 = (instruction & (0b11111 << 15)) >> 16; // Source register 2
    *r3 = (instruction & (0b11111 << 10)) >> 11; // Destination register
    *funct = instruction & (0b111111); // Function field (for R-type instructions)
    *offset = instruction & (0b1111111111111111); // Offset (for I-type instructions)
    *jsec = instruction & (0b11111111111111111111111111); // Jump address (for J-type instructions)
}

// Instruction decode
int instruction_decode(unsigned op, struct_controls *controls) {
    // Initialize all control signals to deasserted state by default
    controls->RegDst = 0x0;
    controls->Jump = 0x0;
    controls->Branch = 0x0;
    controls->MemRead = 0x0;
    controls->MemtoReg = 0x0;
    controls->ALUOp = 0x0;
    controls->MemWrite = 0x0;
    controls->ALUSrc = 0x0;
    controls->RegWrite = 0x0;

    // Set control signals based on opcode
    switch (op){

        case 0b000000: // R-type instruction
            controls->RegDst = 0x1; // Use r3 as destination register
            controls->ALUOp = 0x7; // ALU operation determined by funct field
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b000010: // Jump instruction
            controls->RegDst = 0x2; // Jump instruction, no destination
            controls->Jump = 0x1; // Set Jump signal
            break;

        case 0b000100: // Branch if equal instruction
            controls->RegDst = 0x2; // Branch instruction, no destination
            controls->Branch = 0x1; // Set Branch signal
            controls->MemtoReg = 0x2; // Don't write result to register
            break;

        case 0b001000: // Add immediate instruction
            controls->ALUSrc = 0x1; // Use immediate value as second operand
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b001010: // Set less than immediate instruction
            controls->ALUOp = 0x2; // ALU operation is set less than
            controls->ALUSrc = 0x1; // Use immediate value as second operand
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b001011: // Set less than immediate unsigned instruction
            controls->ALUOp = 0x3; // ALU operation is set less than unsigned
            controls->ALUSrc = 0x1; // Use immediate value as second operand
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b001111: // Load upper immediate instruction
            controls->ALUOp = 0x6; // ALU operation is shift left by 16 bits
            controls->ALUSrc = 0x1; // Use immediate value as second operand
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b100011: // Load word instruction
            controls->MemRead = 0x1; // Read from memory
            controls->MemtoReg = 0x1; // Write result to register
            controls->ALUSrc = 0x1; // Use offset as second operand
            controls->RegWrite = 0x1; // Enable register write
            break;

        case 0b101011: // Store word instruction
            controls->RegDst = 0x2; // Store instruction, no destination
            controls->MemtoReg = 0x2; // Don't write result to register
            controls->MemWrite = 0x1; // Write to memory
            controls->ALUSrc = 0x1; // Use offset as second operand
            break;

        default:
            // If an unsupported opcode is encountered, return 1 to indicate halt condition
            return 1;

    }

    // Return 0 to indicate successful decoding
    return 0;
}


// Read register
void read_register(unsigned r1, unsigned r2, unsigned *Reg, unsigned *data1, unsigned *data2) {
    *data1 = Reg[r1];
    *data2 = Reg[r2];
}

// Sign extend
void sign_extend(unsigned offset, unsigned *extended_value) {
    if (offset & 0x8000) // Check if sign bit is set
        *extended_value = offset | 0xFFFF0000; // Sign extend
    else
        *extended_value = offset;
}

// ALU operations
int ALU_operations(unsigned data1, unsigned data2, unsigned extended_value, unsigned funct,
                   char ALUOp, char ALUSrc, unsigned *ALUresult, char *Zero) {
    // Handle second parameter to ALU
    unsigned B = ALUSrc == 1 ? extended_value : data2;

    // Handle ALU control unit
    if (ALUOp == 7) {
        // Add
        if (funct == 0b100000)
            ALUOp = 0;
            // Subtract
        else if (funct == 0b100010)
            ALUOp = 1;
            // And
        else if (funct == 0b100100)
            ALUOp = 4;
            // Or
        else if (funct == 0b100101)
            ALUOp = 5;
            // Slt
        else if (funct == 0b101010)
            ALUOp = 2;
            // Sltu
        else if (funct == 0b101011)
            ALUOp = 3;
            // Unknown funct code
        else
            return 1;
    }

    ALU(data1, B, ALUOp, ALUresult, Zero);
    return 0;

}

// Read/write memory
int rw_memory(unsigned ALUresult, unsigned data2, char MemWrite, char MemRead,
              unsigned *memdata, unsigned *Mem) {
    if (MemRead) {
        if (ALUresult % 4 != 0) // Check word alignment
            Mem[ALUresult >> 2] = data2; // Assuming Mem is word-addressable
        else
            return 1;
    }
    if (MemWrite) {
        if (ALUresult % 4 != 0) // Check word alignment
            *memdata = Mem[ALUresult >> 2]; // Assuming Mem is word-addressable
        else
            return 1;
    }
    return 0;
}

// Write register
void write_register(unsigned r2, unsigned r3, unsigned memdata, unsigned ALUresult,
                    char RegWrite, char RegDst, char MemtoReg, unsigned *Reg) {
    if(RegWrite == 1){
        if(MemtoReg == 1 && RegDst == 0){
            Reg[r2] = memdata;
        }
        else if(MemtoReg == 0 && RegDst == 1){
            Reg[r3] = ALUresult;
        }
    }
}

// PC update
void PC_update(unsigned jsec, unsigned extended_value, char Branch, char Jump, char Zero,
               unsigned *PC) {
    *PC += 4; // Update PC with PC+4
    if(Jump == 1)
        *PC = (jsec << 2) | (*PC & 0xf0000000);
    if(Zero == 1 && Branch == 1)
        *PC += extended_value << 2;
}

/* I have not used C language code obtained from other students, the
Internet, and any other unauthorized sources, either modified or
unmodified. If any code in my program was obtained from an authorized
source, such as textbook or course notes, that has been clearly noted
as a citation in the comments of the program.
Rodrigo Peixoto
ro655391@ucf.edu
 */
