# Datalogger da estação meteorológica

Firmware do ESP32 com DHT22 que coleta temperatura e umidade e publica no fluxo de telemetria que já existe:

```
ESP32 → MQTT estacoes/<MAC>/dados → Recepção (Python) → Redis → Tratamento → PostgreSQL
```

Hardware e ligações: [docs/hardware.md](docs/hardware.md).

## Payload

Mesmo formato do simulador do INFRA:

```json
{"estacao_id":"24:6F:28:AB:CD:EF","unix_time":1794100000,"temp":23.46,"umid":61.00}
```

- `estacao_id` é o MAC do ESP32, que precisa estar cadastrado como estação no backend.
- `temp` e `umid` são os `local_identifier` dos sensores dessa estação. A lista de sensores fica na tabela `SENSORES` de `include/config.h`: cada linha é um campo do payload.
- Outros sensores (entrada analógica ou digital) entram por essa tabela, sem mudar a rotina de coleta. O valor enviado é o bruto; o sistema converte com o ganho e o offset do cadastro. Passo a passo em [docs/hardware.md](docs/hardware.md).
- Leitura que falhou não entra; sem nenhuma leitura válida, nada é publicado.
- Só publica com o relógio sincronizado por NTP.

## Configuração

1. Copie `include/secrets.example.h` para `include/secrets.h` (fora do git) e preencha Wi-Fi e broker.
2. Cadastre a estação com o MAC mostrado na serial (na inicialização ou no comando `status`) e os sensores `temp` e `umid`.
3. O broker precisa aceitar conexão pela rede: no INFRA local a porta do Mosquitto só atende o próprio computador por padrão (ver `MQTT_BIND` no `.env.example` do INFRA).

## Comandos

```bash
pio run -e esp32dev -t upload   # compila e grava
pio device monitor              # serial a 115200
pio test -e native              # testes da lógica, sem placa
```

Pela serial:

- `intervalo 120` muda a frequência de coleta (10 a 3600 s) e grava na flash
- `status` mostra MAC, intervalo, Wi-Fi, MQTT e contadores de coleta e falha

## Organização

| Pasta | O quê |
| --- | --- |
| `lib/coleta` | lógica sem dependência de hardware: agendamento, validação, payload e status (testada em `test/`) |
| `src/main.cpp` | sensor, Wi-Fi, NTP, MQTT e serial |
| `include/` | pinos, identificadores e segredos |

## Frequência de coleta

Padrão 60 s, faixa de 10 a 3600 s (a mesma da coluna `stations.collection_interval_seconds`). Mudança vale a partir da próxima coleta e sobrevive a reinício. Se o loop atrasar, coleta uma vez e reancora, sem rajada.

O backend já guarda a frequência por estação (`GET/PUT /api/stations/:id/collection`). Como ela chega ao ESP32 ainda precisa de decisão do time; por enquanto a troca é pela serial.

## Falhas

- Sensor: `sensor <identificador> sem leitura valida`; os demais são enviados. Sem nenhuma leitura, nada é publicado (e há aviso depois de 3 coletas seguidas).
- Comunicação: Wi-Fi reconecta sozinho; MQTT tenta de novo com espera de 1 s até 60 s, sem travar a coleta.
- `status` separa falhas de sensor de falhas de comunicação. Estação sem comunicação e ocorrências continuam com o backend, que percebe pelo silêncio da estação.
