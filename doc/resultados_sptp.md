# Validação do MAX_SILENCE escolhido

cpu model: i7-1255U
cores: 12

--- logs/sptp-drift/latest/logs/vm1.log ---
[init] start
[init] executando /main
[RSU] master SPTP pronto. cenario validado.

--- logs/sptp-drift/latest/logs/vm2.log ---
[init] start
[init] executando /main
[sptp-drift][master] cenario validado.

--- logs/sptp-drift/latest/logs/vm3.log ---
[init] start
[init] executando /main
[sptp-drift][slave] baseline: raw_offset=3636 us
[sptp-drift][slave] t=0s raw_offset=3712 us drift=76 us
[sptp-drift][slave] t=2s raw_offset=3949 us drift=312 us
[sptp-drift][slave] t=4s raw_offset=3563 us drift=-73 us
[sptp-drift][slave] t=6s raw_offset=3447 us drift=-189 us
...

...
[sptp-drift][slave] RESUMO samples=116 drift_max_abs=503 us drift_medio=-101 us
[sptp-drift][slave] cenario validado.

[test] cenario sptp-drift aprovado.

## Justificativa do MAX_SILENCE_S = 15 s

considerando o drift médio absoluto como 101us em 60s:
slope = 1.68us/s

MAX_SILENCE_S = erro_tolerado / 2 x slope

erro tolerado = 1000 us
slope = 1.68 us/s

MAX_SILENCE_S = 1000 / 3.36

MAX_SILENCE_S = 297.62 segundos

15 segundos fica dentro do tempo de meia vida e ainda tem uma grande margem de segurança