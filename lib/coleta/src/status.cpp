#include "status.h"

#include <cstdio>

namespace coleta {

void StatusColeta::registrar(Evento evento) {
  switch (evento) {
    case Evento::ColetaCompleta:
      coletasCompletas_++;
      falhasSeguidasSensor_ = 0;
      break;
    case Evento::ColetaParcial:
      coletasParciais_++;
      falhasSeguidasSensor_ = 0;
      break;
    case Evento::FalhaSensor:
      falhasSensor_++;
      falhasSeguidasSensor_++;
      break;
    case Evento::PayloadRejeitado:
      payloadsRejeitados_++;
      break;
    case Evento::Publicada:
      publicadas_++;
      break;
    case Evento::FalhaPublicacao:
      falhasPublicacao_++;
      break;
    case Evento::SemConexao:
      semConexao_++;
      break;
    case Evento::ConexaoPerdida:
      conexoesPerdidas_++;
      break;
    case Evento::TentativaReconexao:
      tentativasReconexao_++;
      break;
    case Evento::Reconectado:
      break;
  }
}

size_t StatusColeta::resumo(char* destino, size_t tamanho) const {
  const int n = std::snprintf(
      destino, tamanho,
      "coletas=%lu parciais=%lu falhas_sensor=%lu rejeitadas=%lu publicadas=%lu "
      "falhas_comunicacao=%lu conexoes_perdidas=%lu reconexoes=%lu",
      static_cast<unsigned long>(coletasCompletas_),
      static_cast<unsigned long>(coletasParciais_),
      static_cast<unsigned long>(falhasSensor_),
      static_cast<unsigned long>(payloadsRejeitados_),
      static_cast<unsigned long>(publicadas_),
      static_cast<unsigned long>(falhasComunicacao()),
      static_cast<unsigned long>(conexoesPerdidas_),
      static_cast<unsigned long>(tentativasReconexao_));
  return n < 0 ? 0 : static_cast<size_t>(n);
}

uint32_t atrasoReconexaoMs(uint32_t tentativa) {
  const uint32_t maximo = 60000;
  if (tentativa >= 6) return maximo;  // 2^6 s já passa de 60 s
  return 1000UL << tentativa;
}

}  // namespace coleta
