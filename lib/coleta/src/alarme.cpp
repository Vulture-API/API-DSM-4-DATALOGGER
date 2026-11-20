#include "alarme.h"

namespace coleta {

namespace {
size_t indice(Falha falha) { return static_cast<size_t>(falha); }
}  // namespace

uint32_t persistenciaMs(Falha falha) { return falha == Falha::Configuracao ? 0 : PERSISTENCIA_MS; }

uint8_t bipesDe(Falha falha) { return static_cast<uint8_t>(indice(falha) + 2); }

const char* nomeDe(Falha falha) {
  switch (falha) {
    case Falha::Sensor:
      return "sensor";
    case Falha::Conexao:
      return "conexao";
    case Falha::Hora:
      return "hora";
    case Falha::Configuracao:
      return "configuracao";
  }
  return "?";
}

void Alarme::atualizar(Falha falha, bool ativa, uint32_t agoraMs) {
  const size_t i = indice(falha);
  if (ativa && !ativa_[i]) desdeMs_[i] = agoraMs;
  ativa_[i] = ativa;
}

uint8_t Alarme::confirmadas(uint32_t agoraMs) const {
  uint8_t mascara = 0;
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) {
    const Falha falha = static_cast<Falha>(i);
    if (ativa_[i] && agoraMs - desdeMs_[i] >= persistenciaMs(falha)) mascara |= mascaraDe(falha);
  }
  return mascara;
}

Aviso Alarme::verificar(uint32_t agoraMs) {
  Aviso aviso;
  const uint8_t atuais = confirmadas(agoraMs);
  if (atuais == 0) {
    aviso.normalizou = alarmado_;
    alarmado_ = false;
    anunciadas_ = 0;
    return aviso;
  }
  const bool novaFalha = (atuais & ~anunciadas_) != 0;
  const bool horaDeRepetir = alarmado_ && agoraMs - ultimoToqueMs_ >= REPETICAO_MS;
  anunciadas_ = atuais;  // falha que sumiu sai da lista sem tocar
  if (novaFalha || horaDeRepetir) {
    aviso.falhas = atuais;
    alarmado_ = true;
    ultimoToqueMs_ = agoraMs;
  }
  return aviso;
}

namespace {
// Três sons de caráter bem diferente:
// - abertura: escadinha rápida subindo (alegre, "tlin-tlin-tlin-tlin");
// - falha: sirene ("uí-ó-uí-ó") e o código em bipes graves e lentos;
// - normalizou: um tom agudo longo e liso ("biiiiiip").
constexpr Nota ABERTURA[] = {{1000, 70, 20}, {1400, 70, 20}, {1800, 70, 20}, {2400, 160, 0}};
constexpr Nota SIRENE[] = {{2400, 110, 0}, {1300, 110, 0}, {2400, 110, 0}, {1300, 110, 0}};
constexpr Nota NORMALIZOU[] = {{2000, 900, 0}};

template <size_t N>
constexpr size_t tamanho(const Nota (&)[N]) { return N; }
}  // namespace

void Bipador::comecar(uint32_t agoraMs) {
  quantidade_ = 0;
  inicioMs_ = agoraMs;
}

void Bipador::acrescentar(uint16_t frequenciaHz, uint16_t ligadoMs, uint16_t desligadoMs) {
  if (quantidade_ >= MAXIMO_NOTAS) return;
  notas_[quantidade_++] = Nota{frequenciaHz, ligadoMs, desligadoMs};
}

void Bipador::acrescentar(const Nota* notas, size_t n) {
  for (size_t i = 0; i < n; i++) acrescentar(notas[i].frequenciaHz, notas[i].ligadoMs, notas[i].desligadoMs);
}

void Bipador::tocar(const Aviso& aviso, uint32_t agoraMs) {
  comecar(agoraMs);
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) {
    const Falha falha = static_cast<Falha>(i);
    if ((aviso.falhas & mascaraDe(falha)) == 0) continue;
    acrescentar(SIRENE, tamanho(SIRENE));
    notas_[quantidade_ - 1].desligadoMs = PAUSA_CHAMADA_MS;
    for (uint8_t b = 0; b < bipesDe(falha); b++) {
      const bool ultimo = b + 1 == bipesDe(falha);
      acrescentar(NOTA_CODIGO_HZ, BIPE_MS, ultimo ? PAUSA_GRUPO_MS : PAUSA_BIPE_MS);
    }
  }
  if (aviso.falhas == 0 && aviso.normalizou) acrescentar(NORMALIZOU, tamanho(NORMALIZOU));
}

void Bipador::tocarAbertura(uint32_t agoraMs) {
  comecar(agoraMs);
  acrescentar(ABERTURA, tamanho(ABERTURA));
}

uint32_t Bipador::duracaoMs() const {
  uint32_t total = 0;
  for (size_t i = 0; i < quantidade_; i++) total += notas_[i].ligadoMs + notas_[i].desligadoMs;
  return total;
}

bool Bipador::tocando(uint32_t agoraMs) const { return agoraMs - inicioMs_ < duracaoMs(); }

uint16_t Bipador::frequencia(uint32_t agoraMs) const {
  uint32_t decorrido = agoraMs - inicioMs_;
  for (size_t i = 0; i < quantidade_; i++) {
    if (decorrido < notas_[i].ligadoMs) return notas_[i].frequenciaHz;
    decorrido -= notas_[i].ligadoMs;
    if (decorrido < notas_[i].desligadoMs) return 0;
    decorrido -= notas_[i].desligadoMs;
  }
  return 0;
}

}  // namespace coleta
