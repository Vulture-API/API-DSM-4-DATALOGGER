#include "payload.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace coleta {

const char* descrever(Rejeicao motivo) {
  switch (motivo) {
    case Rejeicao::Nenhuma:
      return "ok";
    case Rejeicao::MacInvalido:
      return "mac_invalido";
    case Rejeicao::RelogioNaoSincronizado:
      return "relogio_nao_sincronizado";
    case Rejeicao::SemLeituraValida:
      return "sem_leitura_valida";
    case Rejeicao::IdentificadorInvalido:
      return "identificador_invalido";
    case Rejeicao::BufferPequeno:
      return "buffer_pequeno";
  }
  return "desconhecido";
}

bool macValido(const char* mac) {
  if (mac == nullptr || std::strlen(mac) != 17) return false;
  for (int i = 0; i < 17; i++) {
    const bool separador = (i % 3) == 2;
    const char c = mac[i];
    if (separador ? c != ':' : !std::isxdigit(static_cast<unsigned char>(c)) ||
                                    std::islower(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

Rejeicao montarPayload(const char* mac, uint32_t unixTime, const Leitura& leitura,
                       const Identificadores& ids, char* destino, size_t tamanho) {
  const Medida medidas[] = {
      {ids.temperatura, leitura.temperatura, leitura.temperaturaOk},
      {ids.umidade, leitura.umidade, leitura.umidadeOk},
  };
  return montarPayload(mac, unixTime, medidas, 2, destino, tamanho);
}

Rejeicao montarPayload(const char* mac, uint32_t unixTime, const Medida* medidas, size_t quantidade,
                       char* destino, size_t tamanho) {
  if (!macValido(mac)) return Rejeicao::MacInvalido;
  if (unixTime < UNIX_TIME_MINIMO) return Rejeicao::RelogioNaoSincronizado;
  if (medidas == nullptr || contarValidas(medidas, quantidade) == 0) return Rejeicao::SemLeituraValida;

  for (size_t i = 0; i < quantidade; i++) {
    if (!identificadorValido(medidas[i].identificador)) return Rejeicao::IdentificadorInvalido;
  }

  size_t usado = 0;
  bool coube = true;
  auto anexar = [&](const char* formato, const char* texto, double valor) {
    if (!coube) return;
    const int n = texto == nullptr ? std::snprintf(destino + usado, tamanho - usado, "%s", formato)
                                   : std::snprintf(destino + usado, tamanho - usado, formato, texto, valor);
    if (n < 0 || static_cast<size_t>(n) >= tamanho - usado) {
      coube = false;
      return;
    }
    usado += static_cast<size_t>(n);
  };

  if (tamanho == 0) return Rejeicao::BufferPequeno;
  const int n = std::snprintf(destino, tamanho, "{\"estacao_id\":\"%s\",\"unix_time\":%lu", mac,
                              static_cast<unsigned long>(unixTime));
  if (n < 0 || static_cast<size_t>(n) >= tamanho) {
    destino[0] = '\0';
    return Rejeicao::BufferPequeno;
  }
  usado = static_cast<size_t>(n);
  for (size_t i = 0; i < quantidade; i++) {
    if (medidas[i].ok) anexar(",\"%s\":%.2f", medidas[i].identificador, medidas[i].valor);
  }
  anexar("}", nullptr, 0);
  if (!coube) {
    destino[0] = '\0';
    return Rejeicao::BufferPequeno;
  }
  return Rejeicao::Nenhuma;
}

bool montarTopico(const char* mac, char* destino, size_t tamanho) {
  if (!macValido(mac)) return false;
  const int escrito = std::snprintf(destino, tamanho, "estacoes/%s/dados", mac);
  return escrito > 0 && static_cast<size_t>(escrito) < tamanho;
}

}  // namespace coleta
