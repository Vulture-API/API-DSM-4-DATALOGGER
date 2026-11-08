#pragma once

#include <cstdint>

namespace coleta {

// Mesma faixa da coluna stations.collection_interval_seconds (SCRUM-587).
constexpr uint32_t INTERVALO_MINIMO_S = 10;
constexpr uint32_t INTERVALO_MAXIMO_S = 3600;
constexpr uint32_t INTERVALO_PADRAO_S = 60;

bool intervaloValido(uint32_t segundos);

// Decide quando coletar a partir do relógio monotônico (millis()).
// A primeira chamada coleta na hora. Se o loop atrasar mais de um intervalo,
// coleta uma vez só e reancora, sem rajada de coletas atrasadas.
class Agendador {
 public:
  explicit Agendador(uint32_t intervaloS = INTERVALO_PADRAO_S);

  bool configurar(uint32_t segundos);
  uint32_t intervaloS() const { return intervaloS_; }
  bool deveColetar(uint32_t agoraMs);

 private:
  uint32_t intervaloS_;
  uint32_t ultimaMs_ = 0;
  bool primeira_ = true;
};

}  // namespace coleta
