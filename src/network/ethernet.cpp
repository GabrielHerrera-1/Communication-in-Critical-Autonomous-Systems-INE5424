#include "ethernet.h"
// BROADCAST fica aqui porque, caso tivessemos colocado ele no .h, todo cpp que fizesse include ethernet.h geraria uma copia dessa variavel
const Ethernet::Address Ethernet::Address::BROADCAST(0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
const Ethernet::Address Ethernet::Address::INTERNAL(0x00,0x00,0x00,0x00,0x00,0x00);
