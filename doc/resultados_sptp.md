# Resultados SPTP

Dados brutos coletados via `make` (cenarios `sptp-simple` + `sptp-drift`) e
`make test-sptp-simple-nosptp` para comparativo.

Logs preservados em `logs/<cenario>/latest/logs/vm*.log`.

## Setup

| Parametro                     | Valor                                            |
|------------------------------|--------------------------------------------------|
| QEMU                         | `qemu-system-x86_64`, `cpu=default`              |
| Pilha de rede                | `Communicator -> Vehicle_Protocol -> NIC -> {SharedMemoryEngine, RawSocketEngine}` |
| Master no SPTP               | vm_id = 1 (`Vehicle(is_master=true)`)            |
| Slaves no SPTP               | vm_id = 2..N                                     |
| `MAX_SILENCE_S` (Traits SPTP)| 15.0 s                                           |
| `MIN_OFFSET_NS` / `MAX_OFFSET_NS` | 100 us / 200 ms                              |
| `INITIAL_DELAY_NS`           | 100 us                                           |
| `INITIAL_RETRY_MS`           | 250 ms (retry rapido pre-1o sync)                |

## Cenario 1: `sptp-simple` (5 VMs)

- Master envia `MASTER_SEND_COUNT = 30` mensagens broadcast, intervalo 500 ms.
- Slaves contam `SLAVE_RECV_TARGET = 18` mensagens, descartam 3 amostras
  iniciais, calculam `min/max/avg_abs` sobre as 22 amostras restantes.
- `delta_us = (slave_realtime_at_recv - master_realtime_at_send) / 1000`.

### Com SPTP ativa

Sync inicial aplicado em cada slave:

| VM  | offset corrigido | delay reportado |
|-----|-----------------:|----------------:|
| vm2 |       -13 907 us |          175 us |
| vm3 |       + 5 841 us |          198 us |
| vm4 |       -24 867 us |          806 us |
| vm5 |        -6 730 us |          838 us |

Residuo apos o sync (22 amostras estaveis):

| VM  | min_us | max_us | avg_abs_us |
|-----|------:|------:|-----------:|
| vm2 | 1 682 | 3 604 |      2 651 |
| vm3 | 1 905 | 3 477 |      2 499 |
| vm4 | 2 746 | 5 759 |      4 458 |
| vm5 | 2 620 | 6 103 |      4 859 |

### Com SPTP desativada (`SO2_SPTP_ENABLE=0`)

Mesmo binario, mesmo cenario, sem `enable_sync` no gateway.

| VM  | min_us  | max_us  | avg_abs_us |
|-----|-------:|-------:|-----------:|
| vm2 | -35 812 | -34 232 |     35 024 |
| vm3 | -13 277 | -11 983 |     12 588 |
| vm4 |  -6 501 |  -5 039 |      5 832 |
| vm5 |  -4 100 |  -2 577 |      3 262 |

### Comparativo direto

| VM  | avg_abs_us COM SPTP | avg_abs_us SEM SPTP | reducao |
|-----|--------------------:|--------------------:|--------:|
| vm2 |               2 651 |              35 024 |   13.2x |
| vm3 |               2 499 |              12 588 |    5.0x |
| vm4 |               4 458 |               5 832 |    1.3x |
| vm5 |               4 859 |               3 262 | 0.7x (slave ja boot proximo) |

## Cenario 2: `sptp-drift` (2 VMs)

- `SO2_SPTP_MAX_SILENCE_S = 3600` (forca apenas 1 sync inicial em 60 s).
- Master envia `MESSAGE_COUNT = 120` mensagens, intervalo 500 ms.
- Slave descarta `WARMUP_DISCARD = 3` amostras, fixa baseline na 4a, mede
  `drift = raw_offset - baseline` ao longo do tempo.
- `raw_offset = slave_realtime_at_recv - master_realtime_at_send`.
- Subtracao da baseline cancela `delay_propagacao` e offset residual constante.

### Sync inicial

| Item                      | Valor          |
|---------------------------|---------------:|
| `[SPTP] sync #1 aplicada` | offset -5 745 us, delay 759 us |
| `baseline raw_offset`     | 4 975 us       |

### Resumo do drift (vm2 slave, 116 amostras em 60 s)

| Metrica         | Valor       |
|-----------------|------------:|
| `drift_max_abs` |     3 237 us |
| `drift_medio`   |      -112 us |
| Slope estimado  | -1.9 us/s (drift_medio / 60 s) |

### Amostras (a cada 2 s)

```
t=0s    raw_offset=4483 us  drift=-491 us
t=2s    raw_offset=4627 us  drift=-347 us
t=4s    raw_offset=4934 us  drift=-40 us
t=6s    raw_offset=4945 us  drift=-29 us
t=8s    raw_offset=4244 us  drift=-731 us
t=10s   raw_offset=4780 us  drift=-194 us
t=12s   raw_offset=4430 us  drift=-544 us
t=14s   raw_offset=5425 us  drift=+450 us
t=16s   raw_offset=5093 us  drift=+118 us
t=18s   raw_offset=5790 us  drift=+815 us
t=20s   raw_offset=4672 us  drift=-302 us
t=22s   raw_offset=5286 us  drift=+311 us
t=24s   raw_offset=4290 us  drift=-684 us
t=26s   raw_offset=4986 us  drift=+10 us
t=28s   raw_offset=4553 us  drift=-422 us
t=30s   raw_offset=5367 us  drift=+392 us
t=32s   raw_offset=4924 us  drift=-50 us
t=34s   raw_offset=5035 us  drift=+59 us
t=36s   raw_offset=4292 us  drift=-682 us
t=38s   raw_offset=4742 us  drift=-232 us
t=40s   raw_offset=4760 us  drift=-215 us
t=42s   raw_offset=4777 us  drift=-198 us
t=44s   raw_offset=4667 us  drift=-307 us
t=46s   raw_offset=4560 us  drift=-415 us
t=48s   raw_offset=4427 us  drift=-547 us
t=50s   raw_offset=4720 us  drift=-255 us
t=52s   raw_offset=4741 us  drift=-233 us
t=54s   raw_offset=4567 us  drift=-407 us
t=56s   raw_offset=4714 us  drift=-260 us
```

## Justificativa do `MAX_SILENCE_S = 15 s`

Aplicando o principio da "meia-vida":

```
MAX_SILENCE_S = erro_tolerado / (2 x drift)
```

| erro_tolerado | drift considerado | MAX_SILENCE_S resultante |
|---------------|------------------:|-------------------------:|
| 1 ms          | drift_medio = 1.9 us/s |              ~263 s |
| 1 ms          | drift_max_abs = 3237 us / 60 s = 54 us/s | ~9.3 s |
| 100 us        | 1.9 us/s          |              ~26 s   |
| 100 us        | 54 us/s           |              ~0.9 s  |

Valor atual de 15 s e compativel com erro tolerado de 1 ms se
considerarmos drift medio observado e algum reforco contra picos de jitter.

## Bias residual sistematico

Em todas as 4 slaves do `sptp-simple`, o residuo medio absoluto fica em
2.5 - 4.9 ms positivo (slave aparenta adiantado em relacao ao master).
Magnitude maior do que `delay/2` (limite teorico do PTP de 4 timestamps com
delay simetrico), portanto nao explicada por assimetria de path. Atribuida ao
overhead entre `recvfrom` no `RawSocketEngine` e a marcacao de `t1'` em
`SPTP_Protocol::on_receive` (delay nao-simetrico em relacao ao `t1` do master,
que e marcado em `Protocol::send_via_nic` proximo da NIC).

## Configuracao do codigo verificada

- Patch de simetria do `t2`: `Protocol::send_via_nic` expoe `tx_ts_out`,
  `SPTP_Protocol::send_sync_request` consome o valor.
- Retry rapido pre-1o sync: enquanto `_first_sync_done == false`, watchdog
  envia novo `REQUEST_SYNC` a cada 250 ms.
- Outlier (>= `MAX_OFFSET_NS`) nao marca `_first_sync_done` nem
  `_last_sync_steady_ns`, garantindo que o watchdog continue tentando.
- Flag de teste `SO2_SPTP_ENABLE=0` desativa `enable_sync` no gateway.
