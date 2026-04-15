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

    // barreira de bootstrap do runtime: gateway e componentes so entram no
    // run() depois que todos montaram protocol/communicator e a recepcao
    // assincrona da SHM ja foi armada de fato.
    static bool wait_until_all_processes_ready();

public:
    SharedMemoryEngine();

    // interface esperada por NIC<Engine>
    void engine_init(const char * ignored);
    int engine_send(const void * frame, unsigned int size);
    int engine_receive(void * frame, unsigned int size);
    void engine_close();
    void engine_get_address(unsigned char * mac);
    void engine_set_nonblocking(bool enabled);

    // inicia recepção: thread bloqueia no semaforo P(), quando V() chega
    // drena tudo e chama on_receive pra cada frame
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
    // explicacao: quando fazemos semget o kernel n cria nomes de semaforos, ele cria
    // um vetor de semaforos indexados por numero
    enum Semaphore_Index {
        SEM_RING_MUTEX = 0, // pra acessar ring buffer e atualizar contadores
        SEM_FREE_SLOTS = 1, // quantos slots podem ser reutilizados
        SEM_GATEWAY_PENDING = 2, // quantas msgs o gateway ainda precisa consumir
        SEM_BOOTSTRAP_MUTEX = 3, // serializa a barreira de startup
        SEM_BOOTSTRAP_RELEASE = 4, // libera gateway e componentes ao mesmo tempo
        SEM_COMPONENT_PENDING_BASE = 5 // quantas msgs o componente i ainda precisa consumir. cada componente tem seu proprio contador de mensagens pendentes
    };

private:
    // calcula qual o SEM_COMPONENT_PENDING_BASE do componente de um dado slot
    // exemplo: slot 0 --> SEM_COMPONENT_PENDING_BASE + 0
    static int sem_component_pending(unsigned int slot);

    // devolve quantos semaforos o semget() precisa criar
    static unsigned int semaphore_count(unsigned int component_count);

    // quando a shm ja existe no kernel, um processo n acessa ela automaticamente
    // ela precisa fazer shmat(shmid, ...) pra anexar a shm no espaço de end dele
    // e depois fazer shmdt pra desanexar. esses metodos fazem isso
    bool attach_region();
    void detach_region();
    bool set_receiver_ready(bool ready);

    // responde em qual semaforo essa instancia deve esperar por mensagens. evita codigo desnecessario dps
    int pending_semaphore() const;

    // consulta o cadastro da shm pra saber se o componente daquele slot ta ativo
    bool is_component_active(unsigned int slot) const;

    // informa se o gateway participa do consumo do ring
    bool is_gateway_active() const;

    // quantos leitores ativos consomem cada slot do ring
    unsigned int active_reader_count() const;

    // quantos leitores uma mensagem escrita por um componente deve ter. esse valor vai pra remaining readers
    unsigned int delivery_count_for_component_write() const;

    // quantos leitores uma msg escrita pelo gateway deve ter
    unsigned int delivery_count_for_gateway_write() const;

    // decide se o slot consumido deve ser entregue pra esta instancia
    bool should_deliver_slot(const SHM::Broadcast_Slot & slot) const;


    // wrappers dos semaforos system V

    // p() no semaforo indicado
    bool wait_semaphore(int sem_index);
    // versao nao bloqueante
    bool try_wait_semaphore(int sem_index);
    // faz v() no semaforo indicado
    bool signal_semaphore(int sem_index);

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
    uint64_t _next_seq;

    ReceiveHandler _on_receive;
    std::thread _worker;
    std::atomic<int> _running_receiver{0};
};

#endif
