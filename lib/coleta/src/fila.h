#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace coleta {

// Mensagens que ainda não saíram pelo MQTT, da mais antiga para a mais nova.
// Com o broker fora do ar a coleta continua e as leituras ficam aqui; ao
// reconectar saem na ordem, cada uma com o unix_time da medida. Cheia, a mais
// antiga dá lugar à nova. Fica na RAM: um reinício da placa perde a fila.
template <size_t CAPACIDADE, size_t TAMANHO>
class FilaEnvio {
 public:
  // false se a mensagem não cabe no espaço de cada item (nada é guardado).
  bool guardar(const char* mensagem) {
    const size_t n = std::strlen(mensagem);
    if (n >= TAMANHO) return false;
    if (quantidade_ == CAPACIDADE) {
      remover();
      descartadas_++;
    }
    std::memcpy(itens_[(inicio_ + quantidade_) % CAPACIDADE], mensagem, n + 1);
    quantidade_++;
    return true;
  }

  const char* primeira() const { return quantidade_ ? itens_[inicio_] : nullptr; }

  void remover() {
    if (quantidade_ == 0) return;
    inicio_ = (inicio_ + 1) % CAPACIDADE;
    quantidade_--;
  }

  bool vazia() const { return quantidade_ == 0; }
  size_t tamanho() const { return quantidade_; }
  uint32_t descartadas() const { return descartadas_; }

 private:
  char itens_[CAPACIDADE][TAMANHO];
  size_t inicio_ = 0;
  size_t quantidade_ = 0;
  uint32_t descartadas_ = 0;
};

}  // namespace coleta
