#include "confd.h"

Confd::Confd(EepromCli &eeprom)
: _eeprom(eeprom)
{
    //_eeprom = eeprom;
}

uint8_t Confd::read_kwh(float *kwh)
{
    return _eeprom.read_float(KWH_ADDRESS, kwh);
}

uint8_t Confd::write_kwh(float kwh)
{
    return _eeprom.write_float(KWH_ADDRESS, kwh);
}
