#ifndef CONFD_H
#define CONFD_H

#include <eeprom_cli.h>
/*
#define KEY_SAVED_FLAF_ADDR (KEY_ADDR + KEY_SIZE)
#define KEY_SAVED_FLAG_VAL 0xAA
#define FIRST_RUN_ADDR (KEY_SAVED_FLAF_ADDR + 1)
#define FIRST_RUN_VAL 0xBB
*/
#define START_ADDRESS 0x0000
#define KWH_ADDRESS (START_ADDRESS + 10)
#define KWH_SIZE 4 // Need 4 bytes


class Confd
{
public:
    Confd(EepromCli &eeprom);
    uint8_t read_kwh(float *kwh);
    uint8_t write_kwh(float kwh);

private:
    EepromCli& _eeprom;
};

#endif
