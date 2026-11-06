# Roteiro sugerido para o vídeo da Aula 4

O enunciado requer **vídeo no YouTube como não listado, rosto visível, identificação e apresentação oral**, mostrando funcionamento com MQTT e explicando hardware e código.

1. **Apresentação e identificação** — diga o nome, a disciplina e o objetivo (botão de emergência acadêmico, virtual).
2. **Componentes** — mostre no Wokwi ESP32, DHT22, botão, LED, buzzer e resistores; explique GPIO15, 18, 23 e 25.
3. **Software** — mostre `setup()` e `loop()`, leitura do sensor, botão e callbacks MQTT.
4. **Broker** — explique publicador, assinatura, tópicos e por que o ESP32 tem Wi-Fi integrado.
5. **Demonstração 1** — mostre temperatura e umidade saindo do Wokwi e chegando ao MQTTX.
6. **Demonstração 2** — acione o botão `AJUDA`: LED e buzzer, com evento de emergência no MQTTX.
7. **Demonstração 3** — envie `ALERTA` e `PARAR` pelo MQTTX, mostrando a resposta dos atuadores.
8. **Resultados** — mostre a série completa com 4 medições por componente, médias e gráficos; indique que RTT não é tempo de um único sentido.
9. **Limitações e encerramento** — destaque que é **simulação**, não dispositivo de uso real; não afirmar que Telegram funciona.

Depois de publicar o vídeo, inserir o link no artigo científico final, na seção Resultados. O link do GitHub também precisa estar nessa seção.
