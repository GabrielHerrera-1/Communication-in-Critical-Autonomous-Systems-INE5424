# Etapa 4 — Sincronização Espacial

Documento detalhado da implementação da Etapa 4: o módulo de kernel GPS, a
filtragem por quadrantes na NIC, a etiquetagem das mensagens e o cenário de
teste. Cobre cada arquivo tocado e os fluxos ponta a ponta.

---

## 1. O que a etapa pede

Do enunciado:

> As mensagens (classe `Message`) devem ser etiquetadas com coordenadas
> espaciais na origem. Os sistemas autônomos devem sincronizar suas posições
> relativas com precisão suficiente para posicioná-los em quadrantes
> específicos. A sincronização espacial deve ser feita através de um **módulo
> de kernel** com uma interface que retorne as coordenadas de cada VM. As
> coordenadas começam aleatórias e passam a simular um deslocamento.

Traduzindo para regras concretas (enunciado + orientações do professor):

1. **Espaço dividido em 4 quadrantes.** Cada VM está em um deles.
2. **Veículos só se comunicam se estiverem no mesmo quadrante.**
3. A posição vem de um **módulo de kernel** acessado por **`ioctl`** (nunca
   `read`/`write`).
4. O deslocamento é um **`rand % 4`** disparado **a cada ≥ 3 s** — o veículo
   permanece um tempo no quadrante antes de trocar.
5. **Sem threads no kernel**: usa-se apenas o *timer do sistema* (`jiffies`).
   O avanço acontece de forma preguiçosa, na chamada de `ioctl`.
6. O **quadrante viaja no frame Ethernet**; a **NIC faz o `drop`** da mensagem
   antes de entregá-la ao protocolo se o quadrante da origem ≠ o local.
7. A **RSU é fixa** — não se desloca. Ela é o único nó com `is_master = true`,
   e usamos essa flag para congelar o GPS dela.

---

## 2. Contexto da arquitetura (recapitulação)

Para entender onde a Etapa 4 se encaixa, é preciso lembrar como a pilha das
etapas anteriores funciona.

- Cada **VM** (instância QEMU) roda um `Vehicle` ou uma RSU.
- Um `Vehicle` faz `fork()` de um **processo gateway** e de um processo por
  **componente** (sensor/atuador/etc.).
- Comunicação **intra-veículo** (entre componentes da mesma VM): memória
  compartilhada — `NIC<SharedMemoryEngine>`.
- Comunicação **inter-veículo** (entre VMs): raw socket sobre `eth0` —
  `NIC<RawSocketEngine>`. Só o **processo gateway** tem essa NIC.
- O `Protocol` tem as duas NICs e faz a ponte: uma mensagem de um componente
  vai por SHM até o gateway, e o gateway a repassa para o raw socket (e
  vice-versa na recepção).
- `Communicator` é a API do usuário; `Message` é o objeto de mensagem.

```
   Componente ─SHM─> Gateway ─raw socket (eth0)─> Gateway ─SHM─> Componente
   (VM A)                (VM A)                      (VM B)        (VM B)
```

O **GPS da Etapa 4 vive no processo gateway**, junto da `NIC<RawSocketEngine>`:
é a NIC quem pergunta "em qual quadrante estou" ao kernel. Como o estado do
quadrante é **global ao módulo de kernel**, qualquer processo da VM que abrir
`/dev/gps` lê o mesmo valor — é assim que "os componentes de um mesmo sistema
compartilham a percepção do espaço".

---

## 3. O módulo de kernel — `kernel/gps_module/`

### 3.1 `gps_ioctl.h` — a ABI compartilhada

Header incluído **tanto pelo kernel quanto pelo userspace**. Define os números
de `ioctl`:

```c
#define GPS_QUADRANTS 4

#define GPS_IOC_MAGIC        'g'
#define GPS_IOC_GET_QUADRANT _IOR(GPS_IOC_MAGIC, 1, int)  // lê o quadrante atual
#define GPS_IOC_SET_FIXED    _IO(GPS_IOC_MAGIC, 2)        // congela (RSU)
```

`_IOR`/`_IO` são as macros do kernel que codificam, num único `unsigned int`,
a direção da transferência, um número "mágico" (`'g'`) e o tamanho do dado.
Isso evita colisão de comandos entre drivers diferentes. O `#ifdef __KERNEL__`
escolhe `<linux/ioctl.h>` (kernel) ou `<sys/ioctl.h>` (userspace).

### 3.2 `gps.c` — o driver

O módulo é um **char device** com interface só de `ioctl`. Estado global:

```c
static int quadrant;              // quadrante atual: 0..3
static unsigned long last_change; // jiffies da última troca
static bool fixed;                // RSU: quadrante congelado
static DEFINE_MUTEX(gps_lock);    // ioctl pode ser concorrente
```

#### Inicialização — `gps_init()` (registrada via `module_init`)

> **Nota:** o `objtool` do kernel 6.15 rejeita uma função chamada literalmente
> `init_module` ("Magic init_module() function name is deprecated"). A macro
> `module_init(gps_init)` registra `gps_init` como a função de inicialização —
> é o mesmo conceito do `init_module` clássico, com a sintaxe atual. Idem
> `module_exit(gps_exit)` para o `cleanup_module`.

Passos do `gps_init()`:

1. `register_chrdev(0, "gps", &gps_fops)` — **avisa o kernel o que sabemos
   fazer** (a tabela `gps_fops`). O `0` pede ao kernel para **alocar um major
   livre** e devolvê-lo.
2. `class_create` + `device_create` — fazem o `devtmpfs` criar o nó
   `/dev/gps` automaticamente (sem precisar de `mknod` manual).
3. `quadrant = get_random_u32() % GPS_QUADRANTS` — **coordenada inicial
   aleatória**, usando o RNG do próprio kernel.
4. `last_change = jiffies` — marca o instante de início.

#### A tabela de operações — `gps_fops`

```c
static const struct file_operations gps_fops = {
    .owner          = THIS_MODULE,   // ponteiro para o próprio módulo
    .open           = gps_open,
    .release        = gps_release,   // o "close"
    .unlocked_ioctl = gps_ioctl,
};
```

`.owner = THIS_MODULE` faz o kernel contar referências: enquanto `/dev/gps`
estiver aberto, o módulo não pode ser removido. Repare que **não há `.read`
nem `.write`** — "nosso GPS é só open, close e ioctl".

#### O coração — `gps_advance_locked()`

```c
static void gps_advance_locked(void)
{
    if (fixed)                                              // RSU: nunca move
        return;
    if (time_before(jiffies, last_change + GPS_MOVE_INTERVAL)) // < 3s: fica
        return;
    quadrant = get_random_u32() % GPS_QUADRANTS;            // rand % 4
    last_change = jiffies;
}
```

- `GPS_MOVE_INTERVAL = 3 * HZ` — `HZ` é a frequência do tick do kernel
  (`CONFIG_HZ=1000` aqui), então `3 * HZ` = 3 segundos em jiffies.
- `time_before(a, b)` é a forma correta de comparar jiffies (lida com o
  *wrap-around* do contador).
- **Sem thread, sem timer**: a função é chamada *dentro do `ioctl`*. Se ainda
  não passaram 3 s, o quadrante simplesmente não muda. Quando passa, sorteia
  um novo. É o "avanço preguiçoso".

#### O handler de ioctl — `gps_ioctl()`

```c
case GPS_IOC_GET_QUADRANT:
    mutex_lock(&gps_lock);
    gps_advance_locked();          // pode trocar de quadrante aqui
    q = quadrant;
    mutex_unlock(&gps_lock);
    if (copy_to_user((void __user *)arg, &q, sizeof(q)))  // kernel -> user
        return -EFAULT;
    return 0;

case GPS_IOC_SET_FIXED:
    mutex_lock(&gps_lock);
    fixed = true;                  // congela: a partir daqui não desloca mais
    mutex_unlock(&gps_lock);
    return 0;
```

- `copy_to_user` é obrigatório: o kernel não pode escrever direto no ponteiro
  do userspace (pode ser inválido/paginado). Ele copia com verificação.
- O `mutex` protege o estado porque dois processos da VM (gateway e
  componentes) podem chamar `ioctl` ao mesmo tempo.
- `GPS_IOC_SET_FIXED` liga a flag `fixed`. Como o estado é **global ao
  módulo**, *todos* os processos da VM passam a ver o quadrante congelado —
  é o que mantém a percepção consistente na RSU.

#### Finalização — `gps_exit()` (via `module_exit`)

`device_destroy` → `class_destroy` → `unregister_chrdev`, na ordem inversa da
criação. É o `cleanup_module` chamado pelo `rmmod`.

### 3.3 `Makefile` do módulo

```makefile
obj-m += gps.o
KDIR ?= .../kernel-build-x86/linux-6.15.5
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

`obj-m += gps.o` diz ao **Kbuild** (o sistema de build do kernel) para compilar
`gps.c` com `-c` em `gps.o` e depois linká-lo num **`gps.ko`** (kernel object).
O `.o` é o passo intermediário; o `.ko` é o que o `insmod`/`ldmod` carrega.

> **Detalhe de build:** a árvore do kernel veio sem `Module.symvers` (gerado só
> quando se compila algum módulo in-tree). Sem ele, o `modpost` não resolve os
> símbolos do kernel. A solução foi rodar `make` na árvore do kernel uma vez
> (o `vmlinux` já existia, então foi rápido) para regenerar o `Module.symvers`.
> Como o kernel foi configurado **sem** `CONFIG_MODVERSIONS` e **sem**
> `CONFIG_MODULE_SIG`, o `gps.ko` carrega sem verificação de CRC/assinatura.

---

## 4. O wrapper userspace — `src/network/gps.h` / `gps.cpp`

Classe `GPS`: abre `/dev/gps` no construtor, fecha no destrutor, e expõe:

- `bool ok()` — `true` se `/dev/gps` abriu.
- `uint8_t quadrant()` — faz `ioctl(GPS_IOC_GET_QUADRANT)` e devolve 0..3.
- `void set_fixed(bool)` — se `true`, faz `ioctl(GPS_IOC_SET_FIXED)`.
- `QUADRANT_NONE = 0xFF` — sentinela para "GPS indisponível".

O `QUADRANT_NONE` é importante para **degradação graciosa**: se o módulo não
estiver carregado (ex.: build nativo, ou os testes da Etapa 3 que rodam sem o
módulo), `open` falha, `quadrant()` devolve `QUADRANT_NONE`, e a NIC trata isso
como "sem filtragem espacial" — a pilha continua funcionando como antes.

---

## 5. O quadrante no frame Ethernet — `src/network/ethernet.h`

O `Ethernet::Frame` ganhou **1 byte** no header:

```c
private:
    Address _dst;                 // 6 bytes
    Address _src;                 // 6 bytes
    uint8_t _type_hi, _type_lo;   // 2 bytes (ethertype)
    uint8_t _quadrant;            // 1 byte  <-- Etapa 4
    uint8_t _payload[MTU];
```

`HEADER_SIZE` passou de **14 → 15**. Acessores: `quadrant()` / `quadrant(q)`.

**Por que no frame Ethernet?** Porque o forwarding entre SHM e raw socket já
faz `memcpy` de `HEADER_SIZE` bytes — colocando o quadrante no header do frame,
ele é **propagado de graça** por toda a ponte SHM↔socket, sem código extra.
Os `static_assert` de tamanho/alinhamento foram conferidos (o frame continua
com alinhamento 1, sem padding).

---

## 6. A NIC e as engines

### 6.1 `NIC::alloc()` — carimba o quadrante no envio

Toda vez que um frame é montado para envio, a NIC carimba o quadrante:

```cpp
f->type(prot);
f->quadrant(Engine::engine_current_quadrant());  // <-- Etapa 4
return buf;
```

Isto é literalmente o "**todo send pede get my location → pede pra NIC → pede
pro kernel**": `alloc` chama `engine_current_quadrant()`, que (na engine de raw
socket) consulta o GPS, que faz o `ioctl`.

### 6.2 `RawSocketEngine` — tem o GPS e faz o `drop`

A engine de raw socket guarda um `GPS _gps` (membro `mutable`, porque o
`engine_should_drop_frame` é `const` mas precisa consultar o kernel). Ela expõe:

- `engine_current_quadrant()` → `_gps.quadrant()` — usado pelo `alloc`.
- `engine_set_fixed(bool)` → `_gps.set_fixed(bool)`.
- `engine_should_drop_frame(...)` — **o `if` na NIC**:

```cpp
bool RawSocketEngine::engine_should_drop_frame(const Ethernet::Frame & frame,
                                               const Ethernet::Address & local) const {
    if (frame.src() == local)              // self-drop (já existia)
        return true;

    uint8_t my_quadrant     = _gps.quadrant();
    uint8_t origin_quadrant = frame.quadrant();
    if (my_quadrant     != GPS::QUADRANT_NONE &&
        origin_quadrant != GPS::QUADRANT_NONE &&
        my_quadrant != origin_quadrant)    // <-- quadrantes diferentes: dropa
        return true;

    return false;
}
```

Esse método é chamado no callback de recepção da NIC, **antes** de o frame ser
propagado ao protocolo (`Observed::notify`). Se devolve `true`, o buffer é
liberado e a mensagem **nunca chega ao protocolo** — exatamente o que o
professor sugeriu. O teste `QUADRANT_NONE` em qualquer um dos lados desativa a
filtragem (cenário sem o módulo).

### 6.3 `SharedMemoryEngine` — sem filtragem espacial

O tráfego intra-veículo (SHM) **não** tem sincronização espacial — os
componentes de um mesmo sistema compartilham a localização. Por isso a engine
SHM não tem GPS:

- `engine_current_quadrant()` → devolve `GPS::QUADRANT_NONE`.
- `engine_set_fixed(bool)` → no-op.

Esses métodos existem só para o template `NIC<Engine>` compilar igual para as
duas engines. Como a SHM carimba `QUADRANT_NONE` e o `engine_should_drop_frame`
da SHM não filtra por quadrante, o tráfego interno nunca é descartado.

---

## 7. RSU fixa — `Protocol::enable_sync()`

A RSU é o único nó com `is_master = true`. O `Gateway::initialize()` chama
`_protocol->enable_sync(_is_master)`, e foi aí que penduramos o congelamento:

```cpp
void enable_sync(bool is_master) {
    ...
    _is_master = is_master;
    _socket_nic->set_fixed(is_master);   // <-- Etapa 4: RSU congela o GPS
    ...
}
```

`set_fixed(true)` desce até `GPS::set_fixed(true)` → `ioctl(GPS_IOC_SET_FIXED)`
→ liga a flag `fixed` no módulo de kernel. A partir daí, **todos** os processos
da RSU (gateway e componentes) leem sempre o mesmo quadrante. Para os veículos
comuns, `set_fixed(false)` é no-op e o GPS continua sorteando normalmente.

---

## 8. A `Message` etiquetada — `message.h` / `communicator.h`

A `Message` ganhou o campo `_quadrant` e o acessor `quadrant()`. O
preenchimento acontece no `Communicator::receive()`:

```cpp
// o quadrante da origem viajou no header do frame Ethernet; lemos antes
// de _channel->receive() porque ele consome (libera) o buffer.
uint8_t origin_quadrant = buf->data()->quadrant();
...
message->_quadrant = origin_quadrant;
```

Assim a mensagem entregue ao usuário está "**etiquetada com a coordenada
espacial da origem**". Para mensagens intra-veículo (SHM) o quadrante é
`QUADRANT_NONE`, já que a sincronização espacial é entre sistemas distintos.

---

## 9. Fluxos ponta a ponta

### Fluxo A — envio de uma mensagem inter-veículo

```
1. Componente da VM A chama Communicator::send(msg)
2. Protocol::send -> send_via_nic(_socket_nic, ...)   [no processo gateway]
3. NIC::alloc() monta o frame Ethernet e chama
   Engine::engine_current_quadrant()
4.   -> RawSocketEngine -> GPS::quadrant() -> ioctl(GPS_IOC_GET_QUADRANT)
5.     -> gps_ioctl() -> gps_advance_locked():
          se passaram >=3s, sorteia rand%4; senão mantém
6.     -> copy_to_user devolve o quadrante atual
7. NIC carimba f->quadrant(q) no header do frame
8. NIC::send() -> engine_send() -> sai pela eth0 (raw socket, mcast)
```

> Observação: se o emissor é um *componente* (não o gateway), o caminho real é
> componente ─SHM→ gateway ─socket→ rede. O frame que sai na rede é montado
> pelo `_socket_nic->alloc()` **no gateway**, então quem carimba o quadrante é
> o GPS do gateway — a localização do veículo, como esperado.

### Fluxo B — recepção e o `drop` por quadrante

```
1. Frame chega na eth0 da VM B; a engine de raw socket dispara o callback
   de recepção da NIC.
2. A NIC chama Engine::engine_should_drop_frame(frame, _address):
   2a. frame.src() == meu MAC?  -> drop (self-drop)
   2b. my_quadrant = GPS::quadrant() (ioctl no kernel da VM B)
   2c. origin_quadrant = frame.quadrant() (carimbado pela VM A)
   2d. ambos válidos e diferentes?  -> DROP: libera o buffer e RETORNA.
       A mensagem NUNCA chega ao Protocol.
3. Se não dropou: a NIC faz Observed::notify(prot, buf) -> Protocol::update
4. Protocol repassa o frame para a SHM (memcpy de HEADER_SIZE+payload,
   ou seja, o byte do quadrante vai junto)
5. Componente da VM B: Communicator::receive() lê buf->data()->quadrant()
   e preenche message->_quadrant
6. A aplicação recebe a Message etiquetada com o quadrante da origem
```

O ponto-chave: o **`drop` acontece no passo 2d, dentro da NIC**, antes do
protocolo. Veículos em quadrantes diferentes simplesmente não se enxergam.

### Fluxo C — a temporização da troca de quadrante

```
t=0s    : insmod gps.ko -> quadrante inicial aleatório, last_change = jiffies
t=0..3s : qualquer ioctl(GET_QUADRANT) -> gps_advance_locked vê
          time_before(jiffies, last_change+3s) == true -> mantém o quadrante
t>=3s   : próximo ioctl(GET_QUADRANT) -> sorteia rand%4, last_change = jiffies
...      : repete; o veículo permanece >=3s em cada quadrante
```

Não há thread periódica. A "passagem do tempo" é só o `jiffies` lido a cada
`ioctl`. Vários `ioctl` dentro da mesma janela de 3 s devolvem o mesmo
quadrante — é isso que garante que o emissor, o gateway e o receptor de uma VM
concordem sobre a posição naquele instante.

### Fluxo D — a RSU fixa

```
1. RSU = Vehicle(is_master=true)
2. Gateway::initialize() -> Protocol::enable_sync(true)
3.   -> _socket_nic->set_fixed(true) -> GPS::set_fixed(true)
4.     -> ioctl(GPS_IOC_SET_FIXED) -> fixed = true no módulo
5. A partir daí, gps_advance_locked() retorna logo no 'if (fixed)':
   o quadrante da RSU nunca muda, para todos os processos da VM.
```

---

## 10. Integração de build e o cenário de teste

### 10.1 `makefile` principal

- Alvo `gps-module`: compila `kernel/gps_module/gps.ko`.
- `quadrant` entrou em `CORE_TEST_NAMES` (compila `build/tests/quadrant`).
- Alvo `test-quadrant`: depende de `gps-module`, roda o cenário com 5 VMs.
- `test-quadrant` entrou na suíte `test`; `clean` também limpa o módulo.

### 10.2 `tests/run_qemu_test.sh`

- Só o cenário `quadrant` embute o `gps.ko` no initramfs (variável
  `WITH_GPS=1` setada pelo alvo `test-quadrant`). Os outros cenários rodam
  **sem** `/dev/gps` — a NIC vê `QUADRANT_NONE` e não filtra, então a Etapa 3
  continua intacta.
- O `init` do initramfs, se encontrar `/gps.ko`, faz `insmod /gps.ko` antes de
  rodar o binário de teste.

### 10.3 `tests/quadrant.cpp` — 5 VMs

Topologia: **VM1 = RSU** (`Vehicle(is_master=true)`, GPS congelado);
**VM2..VM5 = veículos** (`is_master=false`, GPS se desloca).

Cada VM roda **dois componentes**:

- `QSender` (porta `TEST_QUADRANT_SENDER`, send-only): a cada 500 ms envia uma
  mensagem em broadcast `"quadrant:vmN:seq"` e registra a linha do tempo do
  próprio quadrante.
- `QReceiver` (porta `TEST_QUADRANT_RECEIVER`): em uma thread, recebe mensagens
  e, para cada uma, compara `m.quadrant()` (quadrante da origem, vindo do
  frame) com o próprio quadrante atual (`GPS::quadrant()`). Classifica como
  `same_quadrant` ou `cross_quadrant`. A thread principal amostra o próprio
  quadrante para contar `quad_changes`.

**Por que dois componentes e só o `QReceiver` valida?** O harness do QEMU
considera sucesso quando acha "cenario validado." no log da VM. Se sender e
receiver imprimissem o padrão, uma falha do receiver passaria despercebida
(o sender ainda teria impresso). Então **só o `QReceiver` imprime o padrão de
sucesso**, e só se todas as asserções passarem.

Asserções por VM:

1. **`received >= MIN_RECEIVED`** — a comunicação de fato aconteceu.
2. **`cross_quadrant <= received / 10`** — a filtragem funciona. Com o filtro
   ativo, `cross ≈ 0`. Com o filtro quebrado, ~75% das mensagens seriam
   cross-quadrante (em média 1 em 4 VMs co-localizada), estourando o limite.
3. **RSU: `quad_changes == 0`** — a RSU realmente fica fixa.

Mensagens intra-veículo (do próprio `QSender`, entregues via SHM com quadrante
`NONE`) são ignoradas pelo `QReceiver` — a sincronização espacial é entre
sistemas distintos.

### 10.4 Resultado observado

```
[quadrant][vm1] RESUMO role=RSU     received=193 same=193 cross=0 quad_changes=0
[quadrant][vm2] RESUMO role=veiculo received=186 same=186 cross=0 quad_changes=25
[quadrant][vm3] RESUMO role=veiculo received=191 same=191 cross=0 quad_changes=28
[quadrant][vm4] RESUMO role=veiculo received=213 same=213 cross=0 quad_changes=24
[quadrant][vm5] RESUMO role=veiculo received=209 same=209 cross=0 quad_changes=20
```

- `cross_quadrant = 0` em todas → a NIC só entrega mensagens do mesmo
  quadrante.
- RSU com `quad_changes = 0` → fica fixa; veículos com 20–28 trocas → se
  deslocam.
- A suíte da Etapa 3 (`sptp-simple`) continua passando — o frame 1 byte maior
  não quebrou nada.

---

## 11. Arquivos tocados

**Novos:**

- `kernel/gps_module/gps.c` — o módulo de kernel.
- `kernel/gps_module/gps_ioctl.h` — ABI compartilhada kernel↔userspace.
- `kernel/gps_module/Makefile` — Kbuild do módulo.
- `src/network/gps.h` / `gps.cpp` — wrapper userspace.
- `tests/quadrant.cpp` — cenário de teste com 5 VMs.
- `docs/etapa4_explicacao.md` — este documento.

**Modificados:**

- `src/network/ethernet.h` — campo `_quadrant` no frame, `HEADER_SIZE` 14→15.
- `src/network/nic.h` — `alloc` carimba o quadrante; `set_fixed()` passthrough.
- `src/network/engine/raw_socket_engine.h` / `.cpp` — membro `GPS`,
  `engine_current_quadrant()`, `engine_set_fixed()`, `drop` por quadrante.
- `src/network/engine/shared_memory_engine.h` — `engine_current_quadrant()` e
  `engine_set_fixed()` no-op (sem filtragem intra-veículo).
- `src/channel/protocol.h` — `enable_sync()` chama `set_fixed(is_master)`.
- `src/communication/message.h` — campo `_quadrant` + acessor `quadrant()`.
- `src/communication/communicator.h` — `receive()` preenche `_quadrant`.
- `src/application/component_ports.h` — portas `TEST_QUADRANT_SENDER/RECEIVER`.
- `tests/run_qemu_test.sh` — embute o `gps.ko` (só no cenário `quadrant`).
- `makefile` — alvos `gps-module` e `test-quadrant`; `quadrant` na suíte.

---

## 12. Decisões de projeto e desvios

| Anotação / enunciado | Implementação | Observação |
|---|---|---|
| `ldmod` chama `init_module` | `module_init(gps_init)` | o `objtool` do kernel 6.15 rejeita o nome literal `init_module`; a macro registra a mesma função de init |
| `rmmod` chama `cleanup_module` | `module_exit(gps_exit)` | idem |
| "carregar um `.o` no kernel" | gera `gps.ko` | o `.o` é compilado com `-c`; o Kbuild o linka num `.ko`, que é o que o `insmod` carrega |
| quadrantes 1,2,3,4 | 0,1,2,3 | convenção interna; são 4 quadrantes do mesmo jeito |
| "simulação de 10 s (ex.)" | teste roda 90 s | o "10 s" era exemplo; 90 s dá estatística forte. A troca a cada 3 s bate |
| RSU fixa | `ioctl(GPS_IOC_SET_FIXED)` via `is_master` | "a RSU é o único nó com `is_master=true`, aproveite disso" |
| coordenadas | só o quadrante (0..3) | sem simulação de `(x,y)` nem velocidade — o quadrante É a coordenada espacial pedida pelo enunciado |
