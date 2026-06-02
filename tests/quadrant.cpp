#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message/message.h"
#include "../src/network/gps.h"
#include "../src/application/rsu.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {

static const int          VM_COUNT          = 9;
static const int          FIRST_VEHICLE_VM  = 5;     // vm 1..4 = RSUs; 5..9 = veiculos
static const int          STARTUP_DELAY_S   = 8;     // espera a pilha/SPTP subir
static const int          SIM_DURATION_S    = 90;    // duracao da simulacao
static const unsigned int SEND_INTERVAL_MS  = 1000;
static const int          SAMPLE_INTERVAL_MS = 1000; // amostragem de quadrante
static const int          DRAIN_S           = 3;     // drena mensagens finais
static const int          MIN_RECEIVED      = 8;     // liveness minima 
static const int          RSU_FIX_CHECK_S   = 15;    // janela p/ provar RSU fixa
                                                     // (cruza ~5 intervalos de 3s)

static const char LABEL[] = "quadrant";

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[" << LABEL << "] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[" << LABEL << "] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[" << LABEL << "] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }
    std::cerr << "[" << LABEL << "] parametro so2.vm_id ausente" << std::endl;
    std::exit(1);
}

// Envia mensagens em broadcast e registra a linha do tempo de quadrantes.
// Nunca imprime o padrao de sucesso -- a validacao e do QReceiver.
class QSender : public Component {
public:
    explicit QSender(int vm_id) : Component(LABEL), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_QUADRANT_SENDER;
    }

    // send-only: nao escuta o grupo de broadcast (evita acumular buffers)
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] QSender: communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_S);

        GPS gps;
        if (!gps.ok()) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] QSender: FAIL /dev/gps indisponivel" << std::endl;
            std::exit(1);
        }

        const int iterations = (SIM_DURATION_S * 1000) / SEND_INTERVAL_MS;
        uint8_t last_quad = gps.quadrant();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] QSender inicio quadrante_inicial=" << int(last_quad) << std::endl;

        for (int seq = 1; seq <= iterations; ++seq) {
            uint8_t q = gps.quadrant();
            if (q != last_quad) {
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] deslocou quadrante " << int(last_quad)
                          << " -> " << int(q) << " (seq=" << seq << ")" << std::endl;
                last_quad = q;
            }

            char payload[48];
            std::snprintf(payload, sizeof(payload), "%s:vm%d:%d", LABEL, _vm_id, seq);
            Message m(payload, std::strlen(payload) + 1);
            _communicator->send(&m);

            std::this_thread::sleep_for(std::chrono::milliseconds(SEND_INTERVAL_MS));
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] QSender fim enviou=" << iterations << std::endl;
    }

private:
    int _vm_id;
};

// Recebe mensagens, classifica same/cross-quadrant e valida o cenario.
class QReceiver : public Component {
public:
    QReceiver(int vm_id)
        : Component(LABEL), _vm_id(vm_id){}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_QUADRANT_RECEIVER;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] QReceiver: communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_S);

        GPS gps; // mesma fonte que o gateway: o estado e global ao modulo
        if (!gps.ok()) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] QReceiver: FAIL /dev/gps indisponivel" << std::endl;
            std::exit(1);
        }

        _running.store(true, std::memory_order_release);
        std::thread rx(&QReceiver::receive_loop, this);

        // thread principal: amostra o proprio quadrante (linha do tempo)
        const int samples = (SIM_DURATION_S * 1000) / SAMPLE_INTERVAL_MS;
        uint8_t last_quad = gps.quadrant();
        int quad_changes  = 0;

        for (int i = 0; i < samples; ++i) {
            uint8_t q = gps.quadrant();
            if (q != last_quad) {
                ++quad_changes;
                last_quad = q;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_INTERVAL_MS));
        }

        // deixa as ultimas mensagens chegarem, depois congela a leitura
        std::this_thread::sleep_for(std::chrono::seconds(DRAIN_S));
        _running.store(false, std::memory_order_release);

        const long received = _received.load(std::memory_order_acquire);
        const long same     = _same.load(std::memory_order_acquire);
        const long cross    = _cross.load(std::memory_order_acquire);

        // a thread rx pode estar bloqueada em receive(); o processo encerra
        // ao retornar de run(), entao apenas a soltamos.
        rx.detach();

        // RESUMO consumido pelo makefile (grep RESUMO)
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO role=" << "veiculo"
                  << " received=" << received
                  << " same_quadrant=" << same
                  << " cross_quadrant=" << cross
                  << " quad_changes=" << quad_changes << std::endl;

        // --- assercoes de validacao ---

        // (3) comunicacao aconteceu de fato
        if (received < MIN_RECEIVED) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL recebeu poucas mensagens: received=" << received
                      << " (minimo=" << MIN_RECEIVED << ")" << std::endl;
            std::exit(1);
        }

        // (1) filtragem espacial: com o filtro ativo cross ~ 0; um filtro
        // quebrado entregaria ~75% de cross-quadrant.
        if (cross > received / 10) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL filtragem espacial: cross_quadrant=" << cross
                      << " > received/10=" << (received / 10) << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    // thread dedicada: recebe e classifica cada mensagem como same/cross
    void receive_loop() {
        GPS gps; // fd proprio da thread; o estado do modulo e compartilhado
        while (_running.load(std::memory_order_acquire)) {
            Message m;
            if (!_communicator->receive(&m)) {
                continue;
            }

            int sender_vm = 0, seq = 0;
            const char * payload = reinterpret_cast<const char *>(m.data());
            if (std::sscanf(payload, "quadrant:vm%d:%d", &sender_vm, &seq) != 2) {
                continue;
            }

            // ignora trafego intra-veiculo (do proprio QSender, entregue via
            // SHM com quadrante NONE): a sincronizacao espacial e entre
            // sistemas autonomos distintos. So contamos mensagens inter-VM.
            if (sender_vm == _vm_id) {
                continue;
            }

            uint8_t origin_quad = m.quadrant();       // carimbado na origem
            uint8_t my_quad     = gps.quadrant();     // meu quadrante agora

            _received.fetch_add(1, std::memory_order_relaxed);

            // quadrantes desconhecidos (sem GPS) nao contam para a metrica
            if (origin_quad == GPS::QUADRANT_NONE || my_quad == GPS::QUADRANT_NONE) {
                continue;
            }

            if (origin_quad == my_quad) {
                _same.fetch_add(1, std::memory_order_relaxed);
            } else {
                _cross.fetch_add(1, std::memory_order_relaxed);
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] cross-quadrant from=vm" << sender_vm
                          << " origin_q=" << int(origin_quad)
                          << " my_q=" << int(my_quad)
                          << " seq=" << seq << std::endl;
            }
        }
    }

    int  _vm_id;
    std::atomic<bool> _running{false};
    std::atomic<long> _received{0};
    std::atomic<long> _same{0};
    std::atomic<long> _cross{0};
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    // vm 1..4 sao RSUs: estacoes fixas (uma por quadrante via initial_quadrant
    // no insmod) que atuam como master SPTP. Antes de subir o master, provamos
    // que o quadrante realmente nao se desloca: congelamos com set_fixed -- a
    // mesma chamada que o enable_sync da RSU faz -- e amostramos por uma janela
    // que cruza varios intervalos de 3s. A verificacao roda ANTES de RSU::run()
    // (que imprime "cenario validado." e bloqueia), entao uma falha aqui aborta
    // o no e impede o padrao de sucesso.
    if (vm_id < FIRST_VEHICLE_VM) {
        GPS gps;
        if (!gps.ok()) {
            std::cerr << "[" << LABEL << "][vm" << vm_id
                      << "] RSU: FAIL /dev/gps indisponivel" << std::endl;
            return 1;
        }

        gps.set_fixed(true);
        const uint8_t fixed_quad = gps.quadrant();

        const int samples = (RSU_FIX_CHECK_S * 1000) / SAMPLE_INTERVAL_MS;
        for (int i = 0; i < samples; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_INTERVAL_MS));
            uint8_t q = gps.quadrant();
            if (q != fixed_quad) {
                std::cerr << "[" << LABEL << "][vm" << vm_id
                          << "] FAIL RSU deveria ficar fixa mas quadrante mudou "
                          << int(fixed_quad) << " -> " << int(q) << std::endl;
                return 1;
            }
        }

        std::cout << "[" << LABEL << "][vm" << vm_id
                  << "] RESUMO role=RSU quadrante_fixo=" << int(fixed_quad)
                  << " quad_changes=0" << std::endl;

        // quadrante confirmado fixo: sobe a RSU (master SPTP). run() imprime
        // "cenario validado." e mantem o processo vivo atendendo os slaves.
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    // vm 5..9 sao veiculos: deslocam-se entre quadrantes e validam a filtragem
    // espacial (QSender broadcast + QReceiver classifica same/cross-quadrant).
    Vehicle vehicle(false);
    vehicle.add_component(new QSender(vm_id),
                          Component_Ports::TEST_QUADRANT_SENDER);
    vehicle.add_component(new QReceiver(vm_id),
                          Component_Ports::TEST_QUADRANT_RECEIVER);
    vehicle.initialize();
    vehicle.run();
    return 0;
}
