#pragma once

#include <cstddef>
#include <cstdint>

namespace coleta {

enum class Evento {
  ColetaCompleta,     // todos os sensores leram
  ColetaParcial,      // algum sensor falhou, outro leu
  FalhaSensor,        // nenhum sensor leu
  PayloadRejeitado,   // validação antes da publicação barrou a mensagem
  Publicada,
  FalhaPublicacao,    // MQTT conectado, mas publish falhou
  SemConexao,         // coleta feita com o MQTT fora do ar
  ConexaoPerdida,
  TentativaReconexao,
  Reconectado,
};

// Contadores locais para diagnóstico pela serial. Separa falha de sensor de
// falha de comunicação; estação sem comunicação e ocorrências continuam com o
// backend (SCRUM-425), que já percebe pelo silêncio da estação.
class StatusColeta {
 public:
  void registrar(Evento evento);

  uint32_t coletasCompletas() const { return coletasCompletas_; }
  uint32_t coletasParciais() const { return coletasParciais_; }
  uint32_t falhasSensor() const { return falhasSensor_; }
  uint32_t payloadsRejeitados() const { return payloadsRejeitados_; }
  uint32_t publicadas() const { return publicadas_; }
  uint32_t falhasComunicacao() const { return falhasPublicacao_ + semConexao_; }
  uint32_t conexoesPerdidas() const { return conexoesPerdidas_; }
  uint32_t tentativasReconexao() const { return tentativasReconexao_; }
  uint32_t falhasSeguidasSensor() const { return falhasSeguidasSensor_; }
  bool sensorComProblema() const { return falhasSeguidasSensor_ >= 3; }

  size_t resumo(char* destino, size_t tamanho) const;

 private:
  uint32_t coletasCompletas_ = 0;
  uint32_t coletasParciais_ = 0;
  uint32_t falhasSensor_ = 0;
  uint32_t payloadsRejeitados_ = 0;
  uint32_t publicadas_ = 0;
  uint32_t falhasPublicacao_ = 0;
  uint32_t semConexao_ = 0;
  uint32_t conexoesPerdidas_ = 0;
  uint32_t tentativasReconexao_ = 0;
  uint32_t falhasSeguidasSensor_ = 0;
};

// Espera antes da próxima tentativa de reconexão: 1 s, 2 s, 4 s... até 60 s.
uint32_t atrasoReconexaoMs(uint32_t tentativa);

}  // namespace coleta
