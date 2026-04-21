#ifndef SHARED_MEMORY_ENGINE_H
#define SHARED_MEMORY_ENGINE_H

#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>

#include "shm_region.h"
#include "../ethernet.h"

class SharedMemoryEngine {
public:
    // serve pra agrupar os identificadores dos ipcs system v criados pelo gateway
    struct Context {
        // id da shm retornado pelo shmget
        int shmid;

        // id do conjunto de sem retornados pelo semget
        int semid;
    };

    // dados que cada processo precisa saber 
    struct Configuration {
        Context context; // aponta pros ipcs ja criados (shmid e semid)
        uint16_t slot;   // indice fixo do processo na shm; slot 0 eh reservado ao gateway
        uint16_t port;   // identifica o endpoint logico por tipo de dado
    };

public:
    // gateway cria/destroi os IPCs. por isso precisa desse metodo
    // faz o trabalho de subir a infra da shm
    static Context create(const uint16_t * ports,
                          unsigned int component_count);

    // quando o veiculo é destruido a infra shm se vai tb
    static void destroy(const Context & context);

    // não cria ipc aqui, so registra para o processo atual a config dele
    // processo vai configurar toda sua configuration, com context (que vai ser igual pra todos no mesmo veiculo) e com as demais coisas
    static void configure(const Configuration & configuration);

    // talvez util em testes
    static void clear_configuration();

    // o papel do processo eh definido no bootstrap via slot reservado
    static bool is_gateway_process();


public:
    SharedMemoryEngine();

    // interface esperada por NIC<Engine>
    void engine_init(const char * ignored);
    int engine_send(const void * frame, unsigned int size);
    int engine_receive(void * frame, unsigned int size);
    void engine_close();
    void engine_get_address(unsigned char * mac);

    // inicia recepção: thread bloqueia no sem_wait do pending quando acorda
    // chama on_receive pra cada frame entregue
    void start_receiving();
    // na SHM o self-drop é decidido pelo writer_slot, não pelo MAC
    bool engine_should_drop_frame(const Ethernet::Frame & frame,
                                  const Ethernet::Address & local_address) const;
    // callback chamado pela engine quando um frame chega.
    // a NIC registra esse callback no construtor dela
    typedef std::function<void(const unsigned char*, size_t)> ReceiveHandler;
    void set_receive_handler(ReceiveHandler handler) { _on_receive = handler; }

private:

    // define a numeração fixa dos semaforos system v dentro do semid unico da shm
    // quando fazemos semget o kernel n cria nomes de semaforos, ele cria um vetor
    // de semaforos indexados por numero
    enum Semaphore_Index {
        SEM_RING_MUTEX   = 0,   // unica regiao critica / unico mutex
        SEM_RING_EMPTY   = 1,   
        SEM_RING_FULL    = 2,   
        SEM_PENDING_BASE = 3    // +slot: contador bloqueante de mensagens pendentes por leitor
                                // slot 0 é o gateway
    };

private:
    // indice do contador pendente do leitor em slot
    static int pending_sem_index(uint16_t slot);

    // devolve quantos semaforos o semget() precisa criar (1 mutex + 1 por leitor registrado)
    static unsigned int semaphore_count(unsigned int registered_slots);

    // quando a shm ja existe no kernel, um processo n acessa ela automaticamente
    // ela precisa fazer shmat(shmid, ...) pra anexar a shm no espaço de end dele
    // e depois fazer shmdt pra desanexar. esses metodos fazem isso
    bool attach_region();
    void detach_region();

    // wrappers dos semaforos system V

    // p() no semaforo indicado
    bool sem_wait(int sem_index);

    // faz v() no semaforo indicado
    bool sem_post(int sem_index);

    // espera slot livre, trava mutex do ring, pega seq atual, escolhe slot fisico, escreve o frame, preenche writer_slot e preenche flags
    int write_slot(const void * frame,
                   unsigned int size,
                   uint16_t writer_slot,
                   uint16_t flags);

    // espera semaforo certo, localiza slot que corresponde ao _next_seq, copia o frame pro buffer do chamador, decrementa remaining readers e avança next seq
    int read_slot(void * frame, unsigned int size);

private:
    static Configuration _configuration;
    static bool _configuration_ready;

    Context _context;
    SHM::Region * _region;
    uint16_t _slot;
    uint16_t _port;
    bool _gateway;
    bool _nonblocking;

    ReceiveHandler _on_receive;
    std::thread _worker;
    std::atomic<int> _running_receiver{0};
};

#endif
