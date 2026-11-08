#pragma once

#include <cstddef>
#include <cstdint>

#include "leitura.h"

namespace coleta {

// 01/01/2024 00:00 UTC. Antes disso o relógio ainda não foi sincronizado por NTP.
constexpr uint32_t UNIX_TIME_MINIMO = 1704067200;

// local_identifier dos sensores cadastrados para a estação no backend.
struct Identificadores {
  const char* temperatura = "temp";
  const char* umidade = "umid";
};

enum class Rejeicao {
  Nenhuma,
  MacInvalido,
  RelogioNaoSincronizado,
  SemLeituraValida,
  IdentificadorInvalido,
  BufferPequeno,
};

const char* descrever(Rejeicao motivo);

bool macValido(const char* mac);

// Monta {"estacao_id","unix_time",<identificador>:valor} conforme o contrato de
// telemetria (tópico estacoes/<MAC>/dados). Só entram as leituras válidas;
// sem nenhuma, a mensagem não é montada.
Rejeicao montarPayload(const char* mac, uint32_t unixTime, const Leitura& leitura,
                       const Identificadores& ids, char* destino, size_t tamanho);

// estacoes/<MAC>/dados
bool montarTopico(const char* mac, char* destino, size_t tamanho);

}  // namespace coleta
