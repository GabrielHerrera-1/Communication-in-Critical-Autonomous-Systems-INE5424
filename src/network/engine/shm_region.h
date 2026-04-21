// shm region é uma area de memoria compartilhada que todos processos da vm enxergam. estamos definindo o formato desses dados aqui
// 1) conecta metadados globais da vm
// 2) registra os componentes. diz quais tao ativos, qual a porta que cada um usa

// pensado para o modelo 1 escrita, n-1 leituras, depois reaproveita

#ifndef SHM_REGION_H
#define SHM_REGION_H

#include <cstdint>
#include "../ethernet.h"

namespace SHM {
    static const uint32_t MAGIC = 0x534F3232u; //SO22. serve pra identificar se a memoria compartilhada foi iniciada corretamente
    static const uint16_t INVALID_SLOT = 0xFFFFu; // representa slot invalido
    static const uint16_t GATEWAY_SLOT = 0u; // slot reservado para o processo gateway
    static const uint16_t GATEWAY_WRITER = 0xFFFEu; // diz que quem escreveu foi o gateway


    static const unsigned int MAX_COMPONENTS = 100; // nro de componentes registrados na regiao
    static const unsigned int SLOT_COUNT = 100; // numero de slots do buffer circular
    static const unsigned int FRAME_SIZE = sizeof(Ethernet::Frame); 

    // flags pensadas pra bitmask. elas podem ser combinadas com um OR
    // ajuda a distinguir se msg foi gerada por componente local ou injetada pelo gatewat apos chegar da rede
    enum Slot_Flags : uint16_t {
        DELIVER_TO_COMPONENTS = 0x0001, // bit 0 ligado. conteudo do slot deve ser entregue aos componentes locais
        DELIVER_TO_GATEWAY = 0x0002, // bit 1 ligado. conteudo deve ser entregue ao gateway
    };

    // registro dos componentes do veiculo
    struct Component_Entry {
        uint16_t port;     // identificador logico do componente ou tipo de componente
        uint16_t slot;     // indice fixo no registro
        uint8_t  active;   // 1 quando ta em uso
        uint64_t read_seq; // proxima seq que esse leitor vai consumir (back-pressure)
    };

    // cada posicao do ring buffer é um Broadcast_Slot. representa msg ou frame em circulação
    struct Broadcast_Slot {
        uint16_t      writer_slot;  // indica quem escreveu o slot
        uint16_t      flags;        // para slot flags definidas acima. destino/origem do slot
        uint32_t      frame_size;
        uint16_t      readers_left;       // alinhamento, nao usado
        unsigned char frame[FRAME_SIZE];
    };

    // representa o ring buffer como um todo
    // quando algm for publicar um novo frame pega next write seq, escreve nesse slot e incrementa o next write seq
    struct Broadcast_Ring {
        uint64_t       next_write_seq;   // proxima seq a ser escrita
        Broadcast_Slot slots[SLOT_COUNT]; // array com slots reais do ring buffer
    };

    struct Region {
        uint32_t         magic;
        uint16_t         component_count;
        uint8_t          gateway_active;
        Component_Entry  components[MAX_COMPONENTS];
        Broadcast_Ring   ring;
    };
}

#endif
