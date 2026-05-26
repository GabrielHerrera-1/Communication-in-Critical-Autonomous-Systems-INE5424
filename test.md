Tua intuição está certa — e mais: é exatamente o que o professor descreveu nas mensagens anteriores ("métodos do protocol que são acessíveis na nic p ler esse campo do Header do Frame / usar esses métodos p preencher o frame com o quadrante"). O quadrante pertence ao Header do Protocol, não ao frame Ethernet. Inclusive bate com o formato da mensagem do enunciado: M = {origin, timestamp, payload} — o quadrante é parte do origin.

Deixa eu primeiro apontar o obstáculo real, porque ele muda a escolha.

O obstáculo: dependência circular
A friend class resolve permissão, mas não resolve dependência. Para a NIC ler _quadrant dentro do Protocol::Header, ela precisa conhecer o tipo Protocol::Header. Só que o Protocol é template parametrizado na NIC (Protocol<SharedMemoryNIC, RawSocketNIC>) — ou seja, Protocol já depende da NIC. Se a NIC passar a depender do Protocol, vira ciclo.

A boa notícia: o teu código já resolve esse mesmo problema no SPTP. Olha o sptp_protocol.h:37 — o SPTP precisa chamar Protocol::send sem incluir o Protocol, então recebe um ponteiro de função (SendFn). É o padrão a reaproveitar aqui.

Opção 1 (recomendada) — quadrante no Header, NIC filtra via acessor injetado
Mantém o que o professor quer: GPS na NIC, quadrante no Header do Protocol, e a NIC fazendo a filtragem usando um método do Protocol.

Como fazer: 

Tirar do Ethernet — em ethernet.h: remover _quadrant, getter/setter, voltar HEADER_SIZE para 14 e ajustar o static_assert.

Adicionar ao Protocol::Header — em protocol.h:88: uint8_t _quadrant; com get/set privados.

Acessores estáticos no Protocol que operam sobre payload cru:


static uint8_t read_quadrant(const void* proto_hdr);
static void    write_quadrant(void* proto_hdr, uint8_t q);
Como Header é classe aninhada de Protocol, esses estáticos acessam _quadrant privado naturalmente — nem precisa de friend. A encapsulação que você quer (só a NIC mexe) vem de manter o get/set privado e expor só esses dois estáticos.

Injetar na NIC/Engine os dois ponteiros de função, igual o SendFn do SPTP. A engine guarda uint8_t (_read_q)(const void) etc.

Envio — em send_via_nic (protocol.h:324), depois do alloc, o próprio Protocol grava packet->quadrant(nic->current_quadrant()). (O professor disse "no envio pode ficar em qualquer nível".)

Recebimento/filtragem — em engine_should_drop_frame (raw_socket_engine.cpp:146): origin = _read_q(frame.payload()); if (origin != _gps.quadrant()) drop;. A NIC continua dona da decisão, como hoje.

Custo: a fiação dos ponteiros de função (uns 15-20 min de trabalho), mas é consistente com o SPTP.

Opção 2 (mais simples) — Protocol carimba e filtra, NIC só dá o valor
Quadrante no Protocol::Header (mesma coisa).
Envio: Protocol grava packet->quadrant(socket_nic->current_quadrant()).
Recebimento: o filtro vai para o update() do Protocol (que já parseia o Header): lê packet->quadrant(), compara com socket_nic->current_quadrant(), e não entrega ao componente se diferir.
A NIC expõe só current_quadrant() e set_fixed() (continua dona do GPS).
Vantagem: zero ponteiro de função, zero dependência circular, código mais limpo. Desvantagem: a filtragem sai da NIC e vai pro Protocol — contraria o "deixa tudo na NIC" do professor.

Minha recomendação
Opção 1. O professor foi insistente em "a NIC deve ter acesso no protocol" e "filtragem na NIC" — a Opção 1 honra isso ao pé da letra, reaproveita um padrão que já existe no teu código (não inventa nada novo), e a encapsulação fica exatamente como você imaginou: só a NIC lê/escreve o quadrante, via os acessores que o Protocol expõe. A friend pura que você pensou não basta sozinha por causa do ciclo de tipos; o ponteiro de função é o que fecha isso de forma limpa.

Se o professor for flexível quanto a onde filtra, a Opção 2 é menos código. Mas pelo histórico das mensagens dele, eu iria de Opção 1.

Quer que eu detalhe o passo da injeção dos ponteiros (onde exatamente o Protocol passa os acessores pra NIC) antes de implementar?