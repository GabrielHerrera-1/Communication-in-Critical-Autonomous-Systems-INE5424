# Etapa 5 — Time-Triggered Publish-Subscribe (Interesse/Resposta)

Documento de referência do **código da Etapa 5**. Explica, sem presumir nada, cada
arquivo e cada mecanismo da implementação. Cobre a camada `SmartData` (nova) e as
mudanças de infraestrutura feitas **para** a Etapa 5. A pilha pré-existente (NIC,
SHM, raw socket, SPTP, Ethernet, base do `Protocol`/`Communicator`) é citada apenas
como contexto mínimo.

> Observação: alguns comentários nos arquivos ainda dizem "Transducer" (resquício de
> uma versão anterior). A implementação **atual** usa `IProducer` (a produção do dado
> vive no componente). O texto abaixo descreve o comportamento atual, não o comentário.

---

## 0. Contexto e objetivo

### O que a etapa pede (spec)

Agentes (componentes de um veículo, ou veículos entre si) se comunicam por **mensagens
de Interesse e de Resposta**, num modelo *publish-subscribe sem publish explícito*:

- Quem **quer** um dado envia uma mensagem de **Interesse** (sempre em **broadcast**)
  que designa **inequivocamente o tipo** do dado (código no espírito do **TEDS /
  IEEE 1451**).
- Quem **recebe** o Interesse e **sabe produzir** aquele tipo envia, **periodicamente**,
  mensagens de **Resposta**.
- Cada mensagem carrega um **código que identifica sua natureza** (Interesse vs Resposta).
- Cada Interesse especifica o **período** das respostas; cada agente gerencia seu intervalo.
- **Não há comunicação orientada a eventos** (é tudo time-triggered/periódico).
- A **sincronização temporal pode ser presumida** (o projeto já tem o SPTP da Etapa 4).

Estrutura das mensagens:

```
I = {origin, timestamp, type, period}   // period em us a partir de agora
R = {origin, timestamp, type, value}
```

### Onde a camada se encaixa

A pilha do projeto, de baixo para cima:

```
NIC<SharedMemoryEngine>   (intra-veículo, SHM)      NIC<RawSocketEngine> (inter-veículo)
                 └──────────────┬───────────────────────────┘
                          Protocol  (bridge SHM <-> raw socket; observer por porta)
                                 │
                          Communicator  (endpoint da aplicação; é um observer do Protocol)
                                 │
   ┌─────────────────────────────┴──────────────────────────────┐
   SmartData<Tipo>  (Etapa 5: é um Communicator especializado)   Interest_Tracker (RSU)
   │  emite Interesse / recebe Resposta / responde periodicamente
   │
   componente (IProducer)  — produz o valor do dado
```

A Etapa 5 **não** reescreve a pilha: adiciona a camada `SmartData` em cima do
`Communicator`, com pequenas mudanças de infraestrutura para viabilizar o modelo push
e o rastreamento passivo na RSU.

---

## 1. Visão geral

### Modelo "push"

O `Protocol` já entrega mensagens à aplicação pelo padrão observer: quando um frame da
porta certa chega, ele chama `update(porta, buffer)` no observer. O `Communicator` é um
`Concurrent_Observer`; seu `update()` **padrão** empilha o buffer numa fila, drenada por
`receive()` (modelo clássico, usado pelos testes legados).

O **SmartData é uma subclasse do Communicator que sobrescreve `update()`**: em vez de
empilhar, ele **lê e interpreta** a mensagem (Interesse ou Resposta) na hora. Isso é o
"push": o processamento acontece na própria thread de recepção da NIC, sem fila nem laço
de drenagem na aplicação.

### As threads em jogo (num processo de componente)

| Thread | Origem | Faz o quê |
|---|---|---|
| Recepção da NIC | criada pelo `Protocol`/engine ao anexar | roda o `update()` do SmartData (parse de Interesse/Resposta) |
| Periodic_Thread por binding | criada no responsivo quando chega um interesse | chama `respond()` → produz e envia uma Resposta |
| Periodic_Thread de refresh | criada no interessado | reenvia o Interesse a cada período (e suprime na troca de quadrante) |
| Reaper (cache) | criada no responsivo | expira bindings de subscribers que sumiram |
| Thread da aplicação | `run()` do componente | bloqueia numa `condition_variable` esperando respostas |

---

## 2. Modelo de mensagem

### 2.1 `unit.h` — o "type" (código TEDS)

`Unit` é o código que identifica **inequivocamente o tipo do dado**. Faz parte do
"contrato de fio": dois agentes em VMs diferentes precisam concordar nos valores.

```cpp
enum class Unit : uint32_t {
    NONE            = 0x00000000,
    GPS_POSITION    = 0x00000001,   // posicao (lat, lon)
    SPEED           = 0x00000002,   // velocidade (m/s)
    LIDAR_DISTANCE  = 0x00000003,
    RADAR_DISTANCE  = 0x00000004,
    BRAKE_PRESSURE  = 0x00000010,
    THROTTLE        = 0x00000011,
    STEERING_ANGLE  = 0x00000012,
    TEST_COUNTER    = 0x0000F001,   // contador deterministico p/ testes
};
```

- `enum class ... : uint32_t` → tamanho fixo de 4 bytes (importante por atravessar VMs).
- `TEST_COUNTER` é o tipo usado nos testes (valor determinístico, facilita asserts).

### 2.2 `smart_message.h` — Interesse, Resposta e o cabeçalho

Estas structs viajam **dentro do payload** de uma mensagem. `origin` e `timestamp` da
spec **não** estão aqui: já viajam no cabeçalho do `Protocol` (são preenchidos pelo
`Communicator` no send/receive). Portanto o payload só carrega `{kind, unit, period/value}`.

```cpp
struct SmartHeader {
    enum Kind : uint8_t { INTEREST = 0, RESPONSE = 1 };
    uint8_t kind;   // a "natureza" da mensagem (Interesse vs Resposta)
    Unit    unit;   // o "type" (TEDS)
} __attribute__((packed));

struct InterestMessage {
    SmartHeader header;
    uint64_t    period_us;   // periodo pedido para as respostas
    uint8_t     disinterest; // 1 = cancela o interesse (bit de desinteresse)
} __attribute__((packed));

template <typename Value>
struct ResponseMessage {
    SmartHeader header;
    Value       value;       // o dado produzido
} __attribute__((packed));
```

- `__attribute__((packed))` → sem padding do compilador; o layout no fio é exato.
- `SmartHeader` vem **primeiro** em ambas: reinterpretar os primeiros bytes do payload
  como `SmartHeader*` é sempre válido para descobrir `kind`/`unit` **antes** de saber se
  é Interesse ou Resposta.
- `ResponseMessage<Value>` é parametrizada pelo tipo do valor → é a "mensagem
  especializada" das anotações. O consumidor usa o mesmo `Value` (mesmo `Tipo`), então lê
  de volta corretamente.

**Mapeamento exato com a spec:**

| Campo da spec | Onde está |
|---|---|
| `I.origin` / `R.origin` | cabeçalho do Protocol (MAC + porta de origem) |
| `I.timestamp` / `R.timestamp` | cabeçalho do Protocol (carimbado no envio) |
| `I.type` / `R.type` | `SmartHeader.unit` (payload) |
| `I.period` | `InterestMessage.period_us` (payload) |
| `R.value` | `ResponseMessage.value` (payload) |
| natureza (I vs R) | `SmartHeader.kind` (payload) |

### 2.3 `data_types.h` — descritores de tipo

Um **descritor** associa um `Unit` (código TEDS) ao tipo do `Value`. **Não** contém
lógica de produção — só o tipo. Serve para parametrizar `SmartData<Tipo>` (uma "classe
por tipo de dado").

```cpp
struct Speed_Data  { static constexpr Unit UNIT = Unit::SPEED;
                     struct Value { double mps; }; };
struct Lidar_Data  { static constexpr Unit UNIT = Unit::LIDAR_DISTANCE;
                     struct Value { double meters; }; };
struct Counter_Data{ static constexpr Unit UNIT = Unit::TEST_COUNTER;
                     struct Value { uint64_t seq; }; };
```

Cada descritor expõe `static constexpr Unit UNIT` e `using/struct Value`. É só isso que
o `SmartData<Tipo>` precisa para saber qual Unit filtrar e qual tipo de valor enviar/ler.

### 2.4 `iproducer.h` — quem produz o dado

A **produção do dado vive no componente**, via esta interface. O `SmartData` responsivo
recebe um `IProducer*` e chama `produce()` para gerar cada Resposta.

```cpp
template <typename Value>
class IProducer {
public:
    virtual ~IProducer() {}
    virtual Value produce() = 0;
};
```

No teste, o componente publisher **é** um `IProducer` (herança múltipla: `Component` +
`IProducer<...>`), e `produce()` devolve o valor atual do dado.

---

## 3. Primitivas de apoio

### 3.1 `periodic_thread.h` — `Periodic_Thread`

Executa um `job` a cada período, indefinidamente, até `stop()`. É a primitiva que a spec
pede no responsivo ("nova thread periódica que envia as respostas").

```cpp
class Periodic_Thread {
public:
    Periodic_Thread(uint64_t period_us, std::function<void()> job)
        : _period_us(period_us), _job(std::move(job)),
          _running(true), _thread([this] { loop(); }) {}

    Periodic_Thread(const Periodic_Thread &) = delete;   // não copiável: a thread captura this
    ~Periodic_Thread() { stop(); }

    void stop() { _running.store(false); if (_thread.joinable()) _thread.join(); }
    void set_period(uint64_t p) { _period_us.store(p); }
```

O laço:

```cpp
void loop() {
    auto next = clock::now();                       // clock = steady_clock
    while (_running.load()) {
        _job();
        uint64_t period = _period_us.load();
        next += std::chrono::microseconds(period);  // DEADLINE ABSOLUTO
        auto now = clock::now();
        if (next < now) next = now + microseconds(period); // reancora se atrasou
        // dorme em fatias de até 50ms checando _running (stop() responde rápido)
        while (_running.load() && clock::now() < next) {
            auto slice = next - clock::now();
            auto cap = std::chrono::milliseconds(50);
            std::this_thread::sleep_for(slice < cap ? slice : cap);
        }
    }
}
```

Pontos-chave:
- **Deadline absoluto** (`next += período`): o intervalo não acumula o tempo gasto dentro
  do `job` nem o drift do `sleep`. É por isso que o teste de período mede ~250 ms quase
  exato.
- **Reancoragem** (`if (next < now)`): se o job é lento ou o período é curto demais, evita
  busy-loop tentando "recuperar" o atraso.
- **Sleep em fatias de 50 ms** checando `_running`: o `stop()`/destrutor retorna rápido
  mesmo com período grande, sem precisar de um wait com timeout.
- `set_period()` é atômico → o reajuste (quando chega um interesse com período menor)
  é seguro em runtime.

### 3.2 `binding_cache.h` — `Binding_Cache` (lado responsivo)

Mapeia `Unit -> interesse ativo`. Implementa o algoritmo das anotações: ao chegar um
interesse, se a Unit **não** está na cache cria-se a thread que responde; se **já** está,
apenas atualiza. Como as respostas são broadcast, **uma** thread por Unit serve todos os
interessados daquele tipo (evita N streams redundantes com muitos subscribers).

```cpp
struct Binding {
    uint64_t period_us = 0;
    int64_t  expiry_ns = 0;                       // soft-state
    std::unique_ptr<Periodic_Thread> thread;      // a thread que responde
};
```

Construtor — recebe o callback `respond(Unit)` (que o SmartData fornece) e sobe o reaper:

```cpp
Binding_Cache(std::function<void(Unit)> respond, uint64_t lifetime_min_us,
              unsigned lifetime_factor, uint64_t reaper_period_us)
    : _respond(std::move(respond)), _lifetime_min_us(...), _lifetime_factor(...),
      _running(true), _reaper([this, reaper_period_us]{ reap_loop(reaper_period_us); }) {}
```

**`on_interest`** — o coração ("não está na cache → cria; está → atualiza"):

```cpp
void on_interest(Unit unit, uint64_t period_us) {
    if (period_us == 0) period_us = 1;
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _bindings.find(unit);
    if (it == _bindings.end()) {                          // NÃO está na cache
        Binding b; b.period_us = period_us; b.expiry_ns = expiry_for(period_us);
        auto respond = _respond;
        b.thread = std::make_unique<Periodic_Thread>(     // cria a thread periódica
            period_us, [respond, unit] { respond(unit); });
        _bindings.emplace(unit, std::move(b));
    } else {                                              // JÁ está: atualiza
        Binding & b = it->second;
        if (period_us < b.period_us) { b.period_us = period_us; b.thread->set_period(period_us); }
        b.expiry_ns = expiry_for(b.period_us);            // renova a validade
    }
}
```

- "período menor vence": se dois subscribers pedem períodos diferentes, a thread responde
  no menor (assim todos recebem ao menos na sua taxa).
- cada interesse (inclusive os reenvios) **renova `expiry_ns`** → soft-state.

**`on_disinterest`** — remove o binding na hora (o destrutor do `Binding` faz `stop()` +
`join()` da `Periodic_Thread`):

```cpp
void on_disinterest(Unit unit) {
    std::lock_guard<std::mutex> lock(_mtx);
    _bindings.erase(unit);
}
```

**`expiry_for`** — validade = `max(min_us, factor × período)`, convertido para ns:

```cpp
int64_t expiry_for(uint64_t period_us) const {
    uint64_t life = _lifetime_factor * period_us;
    if (life < _lifetime_min_us) life = _lifetime_min_us;
    return Clock::now_ns() + (int64_t)life * 1000;   // us -> ns
}
```

**`reap_loop`** (thread reaper) — remove bindings expirados; cobre o subscriber que sumiu
**sem** mandar desinteresse (ex.: crash):

```cpp
void reap_loop(uint64_t reaper_period_us) {
    while (_running.load()) {
        // dorme em fatias de 50ms até reaper_period_us
        ...
        int64_t now = Clock::now_ns();
        std::lock_guard<std::mutex> lock(_mtx);
        for (auto it = _bindings.begin(); it != _bindings.end();)
            if (now > it->second.expiry_ns) it = _bindings.erase(it); // expirou
            else ++it;
    }
}
```

> Concorrência: `on_interest`/`on_disinterest` rodam na thread de recepção da NIC (via
> `update()` do SmartData); o `reaper` é outra thread. Ambos mexem em `_bindings` sob
> `_mtx`. O `job` da `Periodic_Thread` (`respond`) **não** mexe na cache, então dá para
> destruir/parar a thread segurando `_mtx` sem risco de deadlock.

---

## 4. Base de recepção (modelo push)

### 4.1 `concurrent_observer.h` — `update()` virou virtual

O `Protocol` notifica os observers chamando `observer->update(c, d)` por um ponteiro
`Concurrent_Observer*`. Para o SmartData sobrescrever `update()` e ser de fato chamado,
esse método **precisa ser virtual** (senão a chamada seria estática e cairia sempre na
base). Foi a única mudança neste arquivo:

```cpp
virtual ~Concurrent_Observer() {}

// virtual para que subclasses (SmartData via Communicator) sobrescrevam e
// interpretem a mensagem na hora, em vez de empilhar.
virtual void update(C c, D * d) {
    std::lock_guard<std::mutex> lock(_mtx);
    _data.push(d);          // comportamento padrão: empilha
    _semaphore.v();
}
D * updated() { _semaphore.p(); ... pop ... }   // drenado por Communicator::receive
```

### 4.2 `communicator.h` — base, send/receive templados, push

O `Communicator` **é um** `Concurrent_Observer` (via `Channel::Observer`). Mudanças da
Etapa 5:

**(a) `send`/`receive` viraram templates** — aceitam `TypedMessage<Payload>` de qualquer
tipo (RawPayload, Interest, Response), deduzindo `Payload`. Assim o mesmo Communicator
manda Interesse e Resposta. Compatível com os testes legados (que usam `Message =
TypedMessage<RawPayload>` → deduz `RawPayload`).

```cpp
template <typename Payload>
bool send(TypedMessage<Payload> * message) {
    message->timestamp(Clock::monotonic_stamp());            // carimba o timestamp
    return _channel->send(_address, Address::logical_broadcast(),  // SEMPRE broadcast
                          message->data(), message->size(), message->timestamp()) > 0;
}

template <typename Payload>
bool receive(TypedMessage<Payload> * message) {
    Buffer * buf = Observer::updated();                      // drena a fila do Concurrent_Observer
    if (!buf) return false;
    Address from; int64_t ts = 0; uint8_t q = MessageHeader::QUADRANT_NONE;
    int size = _channel->receive(buf, &from, &ts, &q, message->data(), sizeof(Payload));
    message->size(size); message->address(from); message->timestamp(ts); message->quadrant(q);
    return size > 0;
}
```

**(b) `update()` padrão (protegido)** — continua empilhando; é um *override* do
`Concurrent_Observer::update` (agora virtual):

```cpp
protected:
void update(Condition c, Buffer * buf) override {
    Observer::update(c, buf);   // chamada qualificada (não-virtual) -> empilha
}
```

**(c) Construtor por Port (protegido)** — para subclasses que conhecem só a porta lógica;
deriva o `Address` via `create_address(port)` e **não** faz attach:

```cpp
protected:
Communicator(Channel * channel, typename Channel::Port port, bool subscribe_broadcast = true)
    : _channel(channel), _address(channel->create_address(port)),
      _broadcast_address(Address::logical_broadcast()),
      _subscribed_to_broadcast(subscribe_broadcast) {}   // sem attach aqui
```

**(d) Attach explícito e deferido** — o ctor público (Address) já anexa; subclasses usam
o ctor por Port (sem attach) e chamam `attach_channel()` **no fim** do seu construtor:

```cpp
void attach_channel() {
    if (_attached) return;
    _channel->attach(this, _address);
    if (_subscribed_to_broadcast) _channel->attach(this, _broadcast_address);
    _attached = true;
}
void detach_channel() { /* simétrico; idempotente via _attached */ }
```

> **Por que deferir o attach?** Se o attach acontecesse no construtor da base, uma
> notificação poderia chegar e chamar `update()` virtual **antes** dos membros da
> subclasse (SmartData) existirem — ou cairia no `update()` da base (empilhando num
> buffer que ninguém drena). Anexando só no fim do ctor da subclasse, garante-se que
> `update()` só será chamado quando `this` já é um SmartData completo. O destrutor da
> subclasse chama `detach_channel()` **antes** de destruir seus membros (mesma corrida ao
> contrário).

### 4.3 `protocol.h` — entrega local para a RSU

Mudança da Etapa 5: quando um frame de DADOS chega pela **rede** (raw socket), além de
repassá-lo para a SHM (componentes em outros processos), o Protocol passa a notificar
também os observers **locais** do processo:

```cpp
// dentro de Protocol::update(), no ramo "frame veio do raw socket":
Buffer *fwd_buf = _shm_nic.alloc(...);
if (fwd_buf) { memcpy(...); _shm_nic.send(fwd_buf); }   // repassa p/ SHM (componentes)

// Etapa 5: entrega também aos observers LOCAIS deste processo.
// Em um veículo, o processo gateway não tem Communicator de app -> _observed
// vazio -> no-op (free, igual antes). Na RSU, permite a um Communicator no
// próprio gateway (o rastreador) ouvir os frames da rede (a SHM não faz
// loopback para o writer).
bool notified = _observed.notify(packet->dst_port(), buf);
if (!notified) free_buffer(buf);
```

E **como `origin`/`timestamp` saem do header** — em `Protocol::receive` (usado pelo
SmartData no `update()` para extrair a mensagem do buffer):

```cpp
*from = Address(buf->data()->src(), packet->src_port()); // origin = MAC + porta de origem
*ts   = packet->timestamp();                              // timestamp da mensagem
*quadrant = packet->quadrant();
// copia o payload (SmartHeader + campos) para 'data'; libera o buffer
```

---

## 5. `SmartData<Type>` — o núcleo

`SmartData` **é um Communicator especializado por tipo de dado**. Sobrescreve `update()`
para interpretar Interesse/Resposta na hora (push). Dois papéis, escolhidos pelo construtor.

```cpp
template <typename Type>
class SmartData : public Communicator<Vehicle_Protocol> {
public:
    using Base    = Communicator<Vehicle_Protocol>;
    using Value   = typename Type::Value;
    using Address = Vehicle_Protocol::Address;
    using Port    = Vehicle_Protocol::Port;
    static constexpr Unit UNIT = Type::UNIT;   // a Unit deste SmartData
```

### 5.1 Construtores (as "2 instâncias")

**RESPONSIVO** — liga um `IProducer` (o componente); cria a cache de bindings; só então
anexa ao canal:

```cpp
SmartData(Vehicle_Protocol * channel, IProducer<Value> * producer, Port port)
    : Base(channel, port, /*subscribe_broadcast=*/true),  // ctor por Port: NÃO anexa
      _mode(RESPONSIVE), _producer(producer) {
    _cache = std::make_unique<Binding_Cache>(
        [this](Unit u){ respond(u); },                    // callback de resposta
        Smart_Config::BINDING_LIFETIME_MIN_US,
        Smart_Config::BINDING_LIFETIME_FACTOR,
        Smart_Config::REAPER_PERIOD_US);
    this->attach_channel();    // só agora: membros prontos para o update()
}
```

**INTERESSADO** — anexa, manda o primeiro Interesse e sobe a thread de refresh:

```cpp
SmartData(Vehicle_Protocol * channel, uint64_t period_us, Port port, bool auto_refresh = true)
    : Base(channel, port, true), _mode(INTERESTED), _period_us(period_us ? period_us : 1) {
    this->attach_channel();
    send_interest(false);                                  // primeiro Interesse imediato
    if (auto_refresh)
        _refresh = std::make_unique<Periodic_Thread>(
            Smart_Config::INTEREST_REFRESH_US, [this]{ refresh_tick(); });
    else
        for (int i = 0; i < 2; ++i) { sleep(50ms); send_interest(false); } // reforça e cala
}
```

`Smart_Config` define os tempos:

```cpp
struct Smart_Config {
    static constexpr uint64_t INTEREST_REFRESH_US     = 1'000'000; // reenvia interesse a cada 1s
    static constexpr uint64_t BINDING_LIFETIME_MIN_US = 3'500'000; // binding expira após 3.5s sem refresh
    static constexpr unsigned BINDING_LIFETIME_FACTOR = 4;         // ou 4x o período pedido
    static constexpr uint64_t REAPER_PERIOD_US        = 1'000'000; // reaper roda a cada 1s
};
```

### 5.2 `update()` — o push (protegido)

Override do `update()` do Communicator. **Não empilha**: extrai a mensagem do buffer,
filtra por `unit` e despacha por `kind`.

```cpp
protected:
void update(Base::Condition, Base::Buffer * buf) override {
    unsigned char payload[Vehicle_Protocol::MTU];
    Address from; int64_t ts = 0; uint8_t q = MessageHeader::QUADRANT_NONE;
    int size = this->_channel->receive(buf, &from, &ts, &q, payload, sizeof(payload)); // extrai + libera buf
    if (size < (int)sizeof(SmartHeader)) return;

    const SmartHeader * h = reinterpret_cast<const SmartHeader*>(payload);
    if (h->unit != UNIT) return;                            // filtra pelo tipo deste SmartData

    if (_mode == RESPONSIVE && h->kind == SmartHeader::INTEREST) {
        const InterestMessage * im = reinterpret_cast<const InterestMessage*>(payload);
        if (im->disinterest) { _cache->on_disinterest(UNIT); if (_on_disinterest) _on_disinterest(UNIT); }
        else                   _cache->on_interest(UNIT, im->period_us);
    } else if (_mode == INTERESTED && h->kind == SmartHeader::RESPONSE) {
        const ResponseMessage<Value> * rm = reinterpret_cast<const ResponseMessage<Value>*>(payload);
        { std::lock_guard<std::mutex> lock(_state_mtx);
          _value = rm->value; _value_ts = ts; _has_value = true;
          _producers.insert(mac_key(from)); ++_responses_received; }
        _state_cv.notify_all();                              // acorda wait_for_responses
        if (_on_response_received) _on_response_received(rm->value, Clock::now_ns());
    }
}
```

Notas:
- `this->_channel->receive(...)` chama o **`Protocol::receive`** (não o `Communicator::receive`):
  extrai `from`/`ts`/payload **e libera o buffer**. Por isso o SmartData não empilha nem
  drena fila nenhuma.
- O responsivo só reage a `INTEREST` da sua `UNIT`; o interessado só a `RESPONSE`. Mensagens
  de outras unidades ou do outro papel são ignoradas (return).
- O interessado conta produtores distintos pelo **MAC de origem** (`mac_key(from)`).

### 5.3 API do lado INTERESSADO (pública)

```cpp
operator Value() const { return value(); }   // converte o SmartData no último valor recebido
Value value() const { lock; return _value; }
bool  has_value() const;
uint64_t    response_count()  const;          // total de respostas recebidas
std::size_t producer_count()  const;          // produtores distintos (por MAC)
uint64_t    quadrant_suppressions() const;    // quantas vezes suprimiu o refresh por quadrante

// callback por Resposta recebida (valor + instante de chegada local), fora do lock
void on_response_received(std::function<void(const Value&, int64_t)> cb);

// bloqueia até response_count >= target ou timeout — SEM polling (acordado pela CV no update)
bool wait_for_responses(uint64_t target, int timeout_ms) {
    std::unique_lock<std::mutex> lk(_state_mtx);
    return _state_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                              [&]{ return _responses_received >= target; });
}

// desinteresse explícito (o veículo "saiu"): para o refresh e anuncia o cancelamento 3x
void unsubscribe() {
    if (_mode != INTERESTED || _unsubscribed) return;
    if (_refresh) _refresh->stop();
    _unsubscribed = true;
    for (int i = 0; i < 3; ++i) { send_interest(true); sleep(50ms); } // 3x: 1 msg pode se perder
}
```

`operator Value()` cumpre a nota "operator Value() devolve o valor"; ele **devolve** o
último valor recebido (não converte entre unidades — ver Seção 12).

### 5.4 API do lado RESPONSIVO (pública)

```cpp
void on_response_sent(std::function<void(uint64_t)> cb);       // pós-envio de cada resposta
void on_disinterest_received(std::function<void(Unit)> cb);    // quando processa um desinteresse
uint64_t responses_sent() const;
```

### 5.5 Privados — `respond`, `send_interest`, `refresh_tick`, `mac_key`

**`respond`** — roda na `Periodic_Thread` do binding; é onde o **componente produz o dado**:

```cpp
void respond(Unit) {
    ResponseMessage<Value> r;
    r.header.kind = SmartHeader::RESPONSE; r.header.unit = UNIT;
    r.value = _producer->produce();             // o COMPONENTE gera o valor
    TypedMessage<ResponseMessage<Value>> msg(r);
    this->send(&msg);                           // broadcast; Communicator carimba o timestamp
    uint64_t n = _responses_sent.fetch_add(1) + 1;
    if (_on_response_sent) _on_response_sent(n);
}
```

**`send_interest`** — monta o Interesse (ou desinteresse) e faz broadcast:

```cpp
void send_interest(bool disinterest) {
    InterestMessage im;
    im.header.kind = SmartHeader::INTEREST; im.header.unit = UNIT;
    im.period_us = _period_us; im.disinterest = disinterest ? 1 : 0;
    TypedMessage<InterestMessage> msg(im);
    this->send(&msg);
}
```

**`refresh_tick`** — o job da thread de refresh, com **consciência de quadrante**. Lê o
**mesmo `/dev/gps`** que o gateway usa; ao detectar troca de quadrante, **suprime** o
reenvio do interesse naquele ciclo (deixa os bindings do quadrante antigo expirarem):

```cpp
void refresh_tick() {
    uint8_t qd = _gps.quadrant();
    uint8_t last = _last_quadrant.load();
    if (qd != GPS::QUADRANT_NONE && last != GPS::QUADRANT_NONE && qd != last) {
        _last_quadrant.store(qd);
        _quadrant_suppressions.fetch_add(1);
        return;                                  // troca de quadrante: NÃO reenvia
    }
    _last_quadrant.store(qd);
    send_interest(false);
}
```

> Sem GPS (`QUADRANT_NONE`, build sem o módulo) o `if` nunca dispara → vira um refresh
> comum. Por isso só o teste de quadrante (com `WITH_GPS=1`) exercita a supressão.

**`mac_key`** — empacota os 6 bytes do MAC num `uint64_t` para usar como chave de
"produtor distinto":

```cpp
static uint64_t mac_key(const Address & a) {
    const uint8_t * m = a.paddr().raw();
    uint64_t k = 0; for (int i = 0; i < 6; ++i) k = (k << 8) | m[i]; return k;
}
```

### 5.6 Destrutor (ciclo de vida)

```cpp
~SmartData() override {
    this->detach_channel();                  // para de receber ANTES de destruir membros
    if (_mode == INTERESTED) {
        if (_refresh) _refresh->stop();
        if (!_unsubscribed) send_interest(true);  // best-effort desinteresse
        _refresh.reset();
    }
    _cache.reset();                          // para as threads de resposta + reaper
}
```

Ordem importa: primeiro `detach_channel()` (não chegam mais `update()`), depois para as
threads. O reset do `_cache` destrói os `Binding` → cada `Periodic_Thread` faz `stop()`+`join()`.

### 5.7 Membros

```cpp
// responsivo
IProducer<Value> * _producer;
std::unique_ptr<Binding_Cache> _cache;
std::atomic<uint64_t> _responses_sent;
std::function<void(uint64_t)> _on_response_sent;
std::function<void(Unit)>     _on_disinterest;

// interessado
uint64_t _period_us;
GPS _gps;                                  // lê o /dev/gps existente (quadrante)
std::atomic<uint8_t>  _last_quadrant;
std::atomic<uint64_t> _quadrant_suppressions;
std::unique_ptr<Periodic_Thread> _refresh;
bool _unsubscribed;
mutable std::mutex _state_mtx;
std::condition_variable _state_cv;
Value _value; int64_t _value_ts; bool _has_value;
std::set<uint64_t> _producers;             // MACs distintos
uint64_t _responses_received;
std::function<void(const Value&, int64_t)> _on_response_received;
```

---

## 6. RSU — rastreamento passivo (repetição de interesses)

### 6.1 `interest_tracker.h`

A RSU é uma estação fixa que **ouve passivamente** e **repete** os interesses. O tracker é
**outro Communicator com `update()` sobrescrito** (genérico, não amarrado a uma Unit).

```cpp
class Interest_Tracker : public Communicator<Vehicle_Protocol> {
public:
    Interest_Tracker(Vehicle_Protocol * channel, Port port, uint64_t repeat_us)
        : Base(channel, port, /*subscribe_broadcast=*/true) {
        this->attach_channel();
        _repeater = std::make_unique<Periodic_Thread>(repeat_us, [this]{ repeat_all(); });
    }

    // push: registra os interesses que passam (ignora respostas)
    void update(Base::Condition, Base::Buffer * buf) override {
        unsigned char payload[Vehicle_Protocol::MTU];
        Address from; int64_t ts; uint8_t q = MessageHeader::QUADRANT_NONE;
        int size = this->_channel->receive(buf, &from, &ts, &q, payload, sizeof(payload));
        if (size < (int)sizeof(SmartHeader)) return;
        const SmartHeader * h = reinterpret_cast<const SmartHeader*>(payload);
        if (h->kind != SmartHeader::INTEREST) return;
        const InterestMessage * im = reinterpret_cast<const InterestMessage*>(payload);
        std::lock_guard<std::mutex> lock(_mtx);
        if (im->disinterest) _interests.erase(h->unit);     // subscriber saiu: para de repetir
        else                 _interests[h->unit] = im->period_us;
    }

private:
    void repeat_all() {                                     // job periódico: reemite tudo
        std::vector<std::pair<Unit,uint64_t>> snap;
        { std::lock_guard<std::mutex> lock(_mtx); snap.assign(_interests.begin(), _interests.end()); }
        for (auto & kv : snap) {
            InterestMessage im; im.header.kind = SmartHeader::INTEREST; im.header.unit = kv.first;
            im.period_us = kv.second; im.disinterest = 0;
            TypedMessage<InterestMessage> msg(im);
            this->send(&msg);
        }
    }
    std::mutex _mtx;
    std::map<Unit, uint64_t> _interests;
    std::unique_ptr<Periodic_Thread> _repeater;
};
```

### 6.2 `gateway.h` — getter do Protocol

Para a RSU criar o tracker no **próprio processo gateway** (onde mora o raw socket), o
`Gateway` expõe seu `Protocol`:

```cpp
Vehicle_Protocol * protocol() { return _protocol.get(); }
```

### 6.3 `rsu.cpp` — ativação opcional

A RSU lê `so2.rsu_repeat_us` da **linha de comando do kernel** (a VM não herda env do
host); se setado, instancia o tracker e mantém o processo vivo:

```cpp
uint64_t read_rsu_repeat_us() {       // parseia /proc/cmdline procurando so2.rsu_repeat_us=<us>
    ...
    if (std::sscanf(tok, "so2.rsu_repeat_us=%llu", &v) == 1) return v;
    ...
}

uint64_t repeat_us = read_rsu_repeat_us();
if (repeat_us > 0 && _gateway.protocol()) {
    Interest_Tracker tracker(_gateway.protocol(), Component_Ports::GATEWAY, repeat_us);
    while (true) { pause(); }          // o update() do tracker e o repeater fazem o trabalho
}
```

A peça que faz isso funcionar é o **notify local** da Seção 4.3: sem ele, o gateway da RSU
(que não tem componentes) não receberia os frames da rede (a SHM não faz loopback para o
próprio writer).

---

## 7. Integração com a aplicação

### 7.1 `component.h`

Dois acréscimos:

```cpp
// Componentes SmartData criam seu próprio observer (o SmartData é um Communicator)
// e dispensam o Communicator bruto do Vehicle. Retornando false, o Vehicle só injeta
// o canal e não cria/anexa um Communicator (evita dois observers na mesma porta).
virtual bool wants_raw_communicator() const { return true; }

void set_channel(Vehicle_Protocol * channel) { _channel = channel; }
// ...
Vehicle_Protocol * _channel = nullptr;
```

### 7.2 `vehicle.cpp` — ramificação canal vs communicator

No processo de cada componente, o `Vehicle` sempre injeta o canal; só cria o Communicator
bruto se o componente quiser:

```cpp
Vehicle_Protocol protocol;
c.first->set_channel(&protocol);
c.first->set_port(c.second);

std::unique_ptr<Communicator<Vehicle_Protocol>> communicator;
if (c.first->wants_raw_communicator()) {                 // legado
    communicator = std::make_unique<Communicator<Vehicle_Protocol>>(
        &protocol, protocol.create_address(c.second), c.first->subscribe_logical_broadcast());
    c.first->set_communicator(communicator.get());
}
c.first->run();                                          // SmartData cria o seu próprio observer
```

Por que não criar sempre o Communicator bruto: se ele e o SmartData ficassem ambos
anexados à porta de broadcast, o `notify()` entregaria o mesmo buffer aos dois — um
empilharia, o outro liberaria → uso-após-liberação. Com `wants_raw_communicator()==false`,
só o SmartData fica anexado.

### 7.3 `component_ports.h` — portas de teste

```cpp
static constexpr Port TEST_INTEREST_PUB = 0xF501;
static constexpr Port TEST_INTEREST_SUB = 0xF502;
```

São as portas lógicas dos componentes de teste. O tráfego Interesse/Resposta em si vai
pelo broadcast; estas portas são só a identidade do componente.

---

## 8. Fluxos completos (ponta a ponta)

### 8.1 Interessado (subscriber)

1. `run()` do componente cria `SmartData<Counter_Data> consumer(_channel, PERIODO, _port)`.
2. O ctor anexa ao canal e chama `send_interest(false)` → `Communicator::send` carimba o
   timestamp e faz broadcast para `logical_broadcast`.
3. O Interesse desce: componente → SHM → processo gateway → raw socket (broadcast em eth0).
4. Em **outra VM**, o gateway recebe pelo raw socket, repassa para a SHM, e o processo do
   componente publisher recebe → `update()` do SmartData responsivo.
5. O publisher responde periodicamente (Seção 8.2). A Resposta volta pelo mesmo caminho.
6. No subscriber, o `update()` (push) atualiza `_value`/`_producers`/`_responses_received`
   e faz `_state_cv.notify_all()`.
7. A `run()` do subscriber acorda no `wait_for_responses(N, timeout)` e lê
   `producer_count()` / `operator Value()`.
8. A thread `_refresh` reenvia o Interesse a cada 1s (cobre perda/late joiner; suprime na
   troca de quadrante).
9. Ao sair, `unsubscribe()` faz broadcast do desinteresse 3×.

### 8.2 Responsivo (publisher)

1. `run()` cria `SmartData<Counter_Data> producer(_channel, this, _port)` (o componente é
   o `IProducer`).
2. Chega um Interesse → `update()` → `_cache->on_interest(UNIT, period)`.
3. Se a Unit não estava na cache, a cache cria uma `Periodic_Thread` com período = pedido.
4. A cada período, a thread chama `respond()` → `_producer->produce()` (o **componente**
   gera o valor) → monta `ResponseMessage<Value>` → `send()` (broadcast).
5. Enquanto chegarem refreshs do Interesse, o binding fica vivo (soft-state). Desinteresse
   ou expiração (reaper) encerram a thread.

### 8.3 Threads e liberação de buffers

- Quem chama `update()` é a **thread de recepção da NIC** (no `Protocol`). O `update()` do
  SmartData/Tracker chama `Protocol::receive(buf, ...)` que **libera o buffer** após
  extrair o conteúdo. (No caminho legado, o `Communicator::update` empilha e o `receive()`
  da aplicação é quem libera depois.)
- O `Protocol::update` só libera o buffer se **nenhum** observer foi notificado
  (`if (!notified) free_buffer(buf)`), evitando dupla liberação.

---

## 9. Os 6 testes

Cada teste é um único binário que detecta seu papel pela linha de comando do kernel
(`so2.vm_id=N` em `/proc/cmdline`) e roda em N VMs QEMU pelo `run_qemu_test.sh`. O harness
considera sucesso quando **todas** as VMs imprimem o padrão `cenario validado.`.

| Teste | VMs | O que monta | O que prova / resultado |
|---|---|---|---|
| `interest_basic` | 5 | RSU + 1 subscriber + 3 publishers | **fundamental 1×N**: subscriber conta 3 produtores distintos. `produtores=3 respostas=9` |
| `interest_period` | 3 | RSU + subscriber (mede intervalo) + publisher | aderência ao período: `avg≈250000us` para `period=250000` |
| `interest_lifecycle` | 3 | RSU + subscriber + publisher | **desinteresse**: subscriber coleta, manda desinteresse e sai; publisher recebe e para |
| `interest_scale` | 22 | RSU + subscriber + **20 publishers** | **≥20 veículos**: subscriber ouve `produtores=20` |
| `interest_rsu_repeat` | 3 | RSU(tracker) + subscriber silencioso + publisher tardio | **RSU repete**: publisher que entra tarde só aprende o interesse pela repetição da RSU |
| `interest_quadrant` | 2 (WITH_GPS) | RSU fixa + subscriber móvel | **quadrante**: subscriber suprime o refresh na troca de quadrante (`supressoes=2`) |

### Estrutura típica de um componente de teste

Publisher (produz o dado + dispensa o Communicator bruto):

```cpp
class Publisher_Component : public Component, public IProducer<Counter_Data::Value> {
    bool wants_raw_communicator() const override { return false; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }
    void run() override {
        SmartData<Counter_Data> producer(_channel, this, _port);   // 'this' é o IProducer
        producer.on_response_sent([&](uint64_t n){ /* imprime "cenario validado." */ });
        while (true) pause();                                      // responde indefinidamente
    }
};
```

Subscriber (emite interesse + espera respostas sem polling):

```cpp
class Subscriber_Component : public Component {
    bool wants_raw_communicator() const override { return false; }
    void run() override {
        SmartData<Counter_Data> consumer(_channel, PERIOD_US, _port);
        while (consumer.producer_count() < N && Clock::now_ns() < deadline)
            consumer.wait_for_responses(consumer.response_count() + 1, 2000);
        /* valida producer_count, imprime "cenario validado." */
    }
};
```

Particularidades por teste:
- **period**: usa `on_response_received` para registrar o instante de chegada de cada
  resposta; calcula a média dos intervalos (descartando warmup) e exige a média numa faixa
  folgada do período pedido.
- **lifecycle**: o publisher usa `on_disinterest_received` para anunciar que parou; o
  subscriber chama `unsubscribe()` e dá um `sleep` para o gateway encaminhar o desinteresse.
- **rsu_repeat**: o subscriber usa `auto_refresh=false` (manda o interesse umas vezes e se
  cala); o publisher entra com `sleep(25s)`. Sem a repetição da RSU, ele nunca ouviria o
  interesse.
- **quadrant**: 2 VMs com `WITH_GPS=1`; o subscriber é móvel (seu GPS troca de quadrante a
  cada ~3s) e conta as supressões do refresh.

---

## 10. Build e execução

### Alvos do `makefile` (Etapa 5)

```
make test-interest-basic        # 5 VMs
make test-interest-period       # 3 VMs
make test-interest-lifecycle    # 3 VMs
make test-interest-rsu-repeat   # 3 VMs (APPEND_CMDLINE="so2.rsu_repeat_us=2000000")
make test-interest-quadrant     # 2 VMs, WITH_GPS=1
make test-interest-scale        # 22 VMs, QEMU_MEM=$(INTEREST_SCALE_MEM)
make test-interest              # roda basic + lifecycle + period (suíte leve)
```

Variáveis novas:
- `INTEREST_SCALE_MEM ?= 128` — memória por VM no teste de escala (22 VMs ao mesmo tempo).

### Mudanças no `run_qemu_test.sh`

- `QEMU_MEM=${QEMU_MEM:-512}` — memória por VM parametrizável (o `-m 512` virou `-m
  "$QEMU_MEM"`); o teste de escala usa 128 para caber 22 VMs.
- `APPEND_CMDLINE=${APPEND_CMDLINE:-}` — texto extra anexado à linha de comando do kernel
  (`-append "... so2.vm_id=$vm_index $APPEND_CMDLINE"`); é como `so2.rsu_repeat_us=...`
  chega na RSU dentro da VM.

---

## 11. Mapeamento spec ↔ código

| Requisito | Onde |
|---|---|
| Interesse/Resposta sem publish | `smart_data.h` (não há publish; só Interesse/Resposta) |
| Tudo em broadcast | `communicator.h` `send()` → `logical_broadcast()` |
| Interesse designa o tipo (TEDS) | `unit.h` + `SmartHeader.unit` |
| Código da natureza (I vs R) | `SmartHeader.kind` |
| Período no Interesse + gerência do intervalo | `InterestMessage.period_us` → `Binding_Cache` → `Periodic_Thread` |
| `I = {origin, ts, type, period}` | header do Protocol (origin/ts) + `InterestMessage` |
| `R = {origin, ts, type, value}` | header do Protocol (origin/ts) + `ResponseMessage` |
| Nova thread por interesse | `binding_cache.h::on_interest` ("não na cache → cria thread") |
| Publisher responde indefinidamente | `Periodic_Thread` no binding + `run()` que faz `pause()` |
| Mandar 1 interesse não basta → reenvio | `SmartData` thread `_refresh` |
| RSU ouve e repete interesses | `interest_tracker.h` + `rsu.cpp` + notify local em `protocol.h` |
| Veículos entram/saem em tempos diferentes | `sleep` de startup nos testes + desinteresse + soft-state |
| Bit de desinteresse | `InterestMessage.disinterest` + `unsubscribe()` |
| Parar reenvio na troca de quadrante | `SmartData::refresh_tick` (lê `/dev/gps`) |
| ≥20 veículos | `interest_scale.cpp` (22 VMs, 20 publishers) |
| Um communicator por tipo de dado | `SmartData<Tipo>` é um Communicator por Unit |
| Sincronização temporal presumida | SPTP (Etapa 4); timestamps em `Clock`/header |
| Recebe resposta → atualiza o valor | `update()` ramo RESPONSE → `_value = rm->value` |

---

## 12. Decisões de design e limitações (honesto)

1. **`IProducer` em vez de "transducer"** — as notas do professor sugeriam o SmartData
   responsivo receber um *transducer* (objeto que fornece o valor) em vez de um ponteiro.
   Aqui a produção do dado foi colocada **no componente**, via a interface `IProducer`
   (decisão explícita do autor do projeto). Funcionalmente equivale; difere da sugestão.

2. **`operator Value()` não converte entre tipos** — a nota "operator Value() converte um
   tipo de dado em outro" (ex.: velocidade → aceleração) **não** foi implementada: o
   `operator Value()` apenas **devolve** o último valor recebido
   (`smart_data.h`: `operator Value() const { return value(); }`). É o único item
   **opcional** das notas SmartData ainda em aberto.

3. **1 componente por veículo nos testes** — os veículos de teste têm um único componente
   (publisher ou subscriber). A spec menciona "veículos com **alguns** componentes"; a
   infraestrutura suporta múltiplos, mas os cenários atuais usam um.

4. **`header.kind` ocupa 1 byte, não 1 bit** — as notas mencionam "manda só 1 bit" para a
   natureza da mensagem. Usamos um `uint8_t` por clareza; o custo é desprezível e o efeito
   é o mesmo.

5. **WIP morto no branch** — existem `src/communication/ttps/publisher.h`,
   `ttps/iproducer.h` e `src/communication/message.h` (vazio) de uma tentativa anterior.
   **Não são incluídos por nada** e foram superseded pela camada `smart_data/`. Podem ser
   removidos.
