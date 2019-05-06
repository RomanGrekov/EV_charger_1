#ifndef EEPROM_CLI_H
#define EEPROM_CLI_H

#include <Wire.h>

// Inherrited errors from twi lib from Wire library
// Output   0 .. success
//          1 .. length to long for buffer
//          2 .. address send, NACK received
//          3 .. data send, NACK received
//          4 .. other twi error (lost bus arbitration, bus error, ..)

// Own lib errors
//   10 .. wire is not available

class EepromCli
{
    public:
        EepromCli(uint8_t sda, uint8_t scl, uint16_t address);
        uint8_t read_byte(uint16_t addr, uint8_t *dest);
        uint8_t read_bytes(uint16_t addr, uint8_t *dest, uint16_t size);
        uint8_t read_float(uint16_t addr, float *f);
        uint8_t write_byte(uint16_t addr, uint8_t data);
        uint8_t write_bytes(uint16_t addr, uint8_t *source, uint16_t size);
        uint8_t write_float(uint16_t addr, float f);
        //uint8_t read_key(uint8_t *key);
        //uint8_t write_key(uint8_t *key);
        //uint8_t is_key_saved(bool *result);
        //uint8_t set_key_saved(void);
        //uint8_t set_key_unsaved(void);

    private:
        TwoWire _wire{1}; // 1 - bus number
        uint16_t _eeprom_address;
        void _set_addr(uint16_t address);
};


#endif
