#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "rom.h"

CPU_t* cpu_init(ROM_t* cartridge) {
    CPU_t* cpu = (CPU_t*) malloc(sizeof(CPU_t));

    // Zero out system memory
    memset(cpu->memory, 0, CPU_MEMORY_SIZE);

    // Set up registers
    cpu->reg_A = 0;
    cpu->reg_X = 0;
    cpu->reg_Y = 0;
    cpu->reg_P = 0x34;
    cpu->reg_S = 0xFD;
    cpu->reg_PC = 0x8000; // TODO: Confirm this

    // Connect hardware
    cpu->ppu = ppu_init();
    cpu->apu = apu_init();
    cpu->cartridge = cartridge;
    cpu->cartridge_bank = 0;

    return cpu;
}

void cpu_free(CPU_t* cpu) {
    free(cpu->ppu);
    free(cpu->apu);
    free(cpu);
}

uint8_t cpu_cycle(CPU_t* cpu) {
    uint8_t opcode = cpu_memory_map_read(cpu, cpu->reg_PC++);
    uint8_t arg1, arg2;
    uint16_t absolute_addr;

    switch(opcode) {
        // ADC
        case 0x69: // Immedt
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            cpu->reg_A += arg1;
            return 2;
        case 0x65: // ZeroPg
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            cpu->reg_A += cpu_memory_map_read(cpu, arg1);
            return 3;
        case 0x75: // ZPIdxX
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            cpu->reg_A += cpu_memory_map_read(cpu, arg1 + cpu->reg_X);
            return 4;
        case 0x6D: // Absolu
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            arg2 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            absolute_addr = (((uint16_t) arg1) << 8) | arg2;
            cpu->reg_A += cpu_memory_map_read(cpu, absolute_addr);
            return 4;
        case 0x76: // AbIdxX
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            arg2 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            absolute_addr = (((uint16_t) arg1) << 8) | arg2;
            cpu->reg_A += cpu_memory_map_read(cpu, absolute_addr + cpu->reg_X);
            return 4; // TODO: 5 if page crossed
        case 0x79: // AbIdxY
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            arg2 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            absolute_addr = (((uint16_t) arg1) << 8) | arg2;
            cpu->reg_A += cpu_memory_map_read(cpu, absolute_addr + cpu->reg_Y);
            return 4; // TODO: 5 if page crossed
        case 0x61: // IdxInd
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            cpu->reg_A += cpu_memory_map_read(cpu, arg1 + cpu->reg_X);
            return 6;
        case 0x71: // IndIdx
            arg1 = cpu_memory_map_read(cpu, cpu->reg_PC++);
            cpu->reg_A += cpu_memory_map_read(cpu, arg1 + cpu->reg_X);
            return 5;
    }

    return 2;
}

uint8_t cpu_memory_map_read(CPU_t* cpu, uint16_t address) {
    // The 2KiB of system memory is mapped from $0000-$07FF, but it's also
    // mirrored to $0800-$1FFF 3 times
    if (address < 0x2000) {
        return cpu->memory[address % CPU_MEMORY_SIZE];
    }

    // The PPU's 8 registers are mapped onto $2000-$2007, and mirrored through
    // $3FFF (so they repeat every 8 bytes)
    if (address >= 0x2000 && address < 0x4000) {
        switch(address % 8) {
            case 2:
                return cpu->ppu->reg_PPUSTATUS;
                break;
            case 4:
                return cpu->ppu->reg_OAMDATA;
                break;
            case 7:
                return cpu->ppu->reg_PPUDATA;
                break;
            default:
                return 0;
        }
    }

    // NES APU and I/O registers
    if (address >= 0x4000 && address < 0x4017) {
        return 0;
    }

    // APU and I/O functionality that is usually disabled
    if (address >= 0x4018 && address < 0x401F) {
        return 0;
    }

    // Expansion RAM
    if (address >= 0x4020 && address < 0x5FFF) {
        return 0;
    }

    // Cartridge SRAM
    if (address >= 0x6000 && address < 0x7FFF) {
        return 0;
    }

    // Cartridge PrgROM
    if (address >= 0x8000 && address < 0xFFFF) {
        uint32_t prg_offset = (cpu->cartridge_bank * (PRG_PAGE_SIZE));
        return cpu->cartridge->prg_data[prg_offset + (address - 0x8000)];
    }

    return 0;
}

void cpu_memory_map_write(CPU_t* cpu, uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        cpu->memory[address % CPU_MEMORY_SIZE] = value;
        return;
    }

    if (address >= 0x2000 && address < 0x4000) {
        switch(address % 8) {
            case 0:
                cpu->ppu->reg_PPUCTRL = value;
                return;
            case 1:
                cpu->ppu->reg_PPUMASK = value;
                return;
            case 3:
                cpu->ppu->reg_OAMADDR = value;
                return;
            case 4:
                cpu->ppu->reg_OAMDATA = value;
                return;
            case 5:
                cpu->ppu->reg_PPUSCROLL = value;
                return;
            case 6:
                cpu->ppu->reg_PPUADDR = value;
                return;
            case 7:
                cpu->ppu->reg_PPUDATA = value;
                return;
            default:
                return;
        }
    }

    // NES APU and I/O registers
    if (address >= 0x4000 && address < 0x4017) {
        switch(address) {
            case 0x4014:
                cpu->ppu->reg_OAMDMA = value;
        }
    }

    // Expansion RAM
    if (address >= 0x4020 && address < 0x5FFF) {
        return;
    }

    // Cartridge SRAM
    if (address >= 0x6000 && address < 0x7FFF) {
        return;
    }
}
