#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "certificados.h"
#include <MQTT.h>
#include <RCSwitch.h>
#include <GFButton.h>
#include <Preferences.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

//MUDAR DEPOIS
#define RF_PORT 2
#define B1_PORT 5
#define B2_PORT 6
#define B3_PORT 7
#define LDR_PIN 1

#define EPD_SCK  12  // O pino CLK da tela deve estar aqui
#define EPD_MISO 13  // Não conectado na tela, mas necessário para o ESP iniciar SPI
#define EPD_MOSI 11  // O pino DIN da tela deve estar aqui

// Seus pinos definidos
#define EPD_CS   10
#define EPD_DC   14
#define EPD_RST  15
#define EPD_BUSY 16
//Configurações do MQTT e Blynk
String MQTT_HOST = " mqtt.janks.dev.br";
int MQTT_PORT = 8883;              // TLS
String MQTT_USER = "aula";             // fixo
// String BLYNK_AUTH_TOKEN = "xLNpnJ0e6f3JaeamhnZuKbRhxakC9lhK";   // password do MQTT

// Objetos globais
WiFiClientSecure conexaoSegura;
MQTTClient mqtt(1000);
Preferences preferencias;

//Botões
GFButton button1(B1_PORT);
GFButton button2(B2_PORT);
GFButton button3(B3_PORT);

//RF Switch
RCSwitch mySwitch = RCSwitch();
const unsigned long UP_CODE = 2448754;
const unsigned long DOWN_CODE = 2448756;
const unsigned long STOP_CODE = 2448760;
const int BITS = 24;
U8G2_FOR_ADAFRUIT_GFX fontes;
GxEPD2_290_T94_V2 modeloTela(10, 14, 15, 16);
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> tela(modeloTela);

//Variaveis de controle integrado da persiana com a luz
int fechar = 0;
int luminosidadeAtual = 0;

int estadoMovimentoAtual = 0; // 0 = Parado, 1 = Subindo, 2 = Descendo
unsigned long ultimoTempoMovimento = 0;
const unsigned long TEMPO_TOTAL_PERSIANA = 30000; // 30 segundos = 100%

float metaNum = 40;
int statusPersiana = 0;
int novoStatus = -1;
int comandoPersiana = 0;
int statusPersianaBlynk = 0;
int autoMode = 0; // 0 - manual, 1 - automático
String fonte_atual = "Natural";
String cor_lampada = "Verde";
int intensidade_lampada = 0;

//Variaveis de tempo
unsigned long instanteAnterior = 0; //Instante para checagem do sensor de luz
unsigned long instanteAnterior2 = 0; //Instante para checagem da persiana
unsigned long instanteAnterior3 = 0; //Instante para checagem da persiana
unsigned long instanteAnteriorAjuste = 0;
unsigned long periodoDeChecagem = 10; //em Segundos

void processarMovimentoAnterior() {
    if (estadoMovimentoAtual != 0) { // Se estava se movendo
        unsigned long tempoAtual = millis();
        unsigned long tempoDecorrido = tempoAtual - ultimoTempoMovimento;

        // Regra de 3: 30000ms = 100%. X ms = Y%
        // Y = (tempoDecorrido * 100) / 30000
        int deltaPorcentagem = (tempoDecorrido * 100) / TEMPO_TOTAL_PERSIANA;

        // Se o tempo for muito curto e der 0, mas houve movimento, força 1% (opcional)
        if (deltaPorcentagem == 0 && tempoDecorrido > 300) deltaPorcentagem = 1;

        if (estadoMovimentoAtual == 1) { // Estava SUBINDO
            statusPersiana -= deltaPorcentagem;
        } 
        else if (estadoMovimentoAtual == 2) { // Estava DESCENDO
            statusPersiana += deltaPorcentagem;
        }

        // Travar entre 0 e 100
        if (statusPersiana < 0) statusPersiana = 0;
        if (statusPersiana > 100) statusPersiana = 100;

        // Atualiza a memória e o Blynk
        preferencias.putInt("statusPer", statusPersiana);
        mqtt.publish("ds/blindStatus", String(statusPersiana));
        
        Serial.println("Movimento finalizado. Tempo: " + String(tempoDecorrido) + "ms. Novo Status: " + String(statusPersiana) + "%");
    }
}

// Função que centraliza TODOS os comandos (físicos ou virtuais)
void aplicarComandoPersiana(int comando) {
    Serial.println("Aplicando comando: " + String(comando));

    // 1. Calcula o progresso do movimento anterior até agora
    processarMovimentoAnterior();

    // 2. Executa a ação física e define o novo estado
    if (comando == 0) { // PARAR
        mySwitch.send(STOP_CODE, BITS);
        estadoMovimentoAtual = 0; 
    }
    else if (comando == 1) { // SUBIR
        mySwitch.send(UP_CODE, BITS);
        estadoMovimentoAtual = 1;
    }
    else if (comando == 2) { // DESCER
        mySwitch.send(DOWN_CODE, BITS);
        estadoMovimentoAtual = 2;
    }

    // 3. Reseta o cronômetro para o novo movimento que começou agora
    ultimoTempoMovimento = millis();
}

void checarBotoes() {
    button1.process();
    button2.process();
    button3.process();
}

void desenharTela() {
  // --- 1. PREPARAÇÃO (Fora do Loop) ---
  // Converter tudo para String ou char array aqui fora para não repetir N vezes
  int metaInt = metaNum;
  String strLuzAtual = String(luminosidadeAtual) + "%";
  String strMeta = String(metaInt) + "%";
  String strPersiana = String(statusPersiana) + "%";
  String strLampada = String(intensidade_lampada) + "%";
  String strComando;
    if (comandoPersiana == 1){
      strComando = "Subindo";
    } 
    else if (comandoPersiana == 2){
        strComando = "Descendo"; 
    }
    else {
      strComando = "Parado";
    } 
    // Configurações globais
  fontes.setForegroundColor(GxEPD_BLACK);
  fontes.setBackgroundColor(GxEPD_WHITE);
  
  // Define que queremos atualizar a tela inteira
  tela.setFullWindow(); 

  // --- 2. LOOP DE PÁGINAS ---
  tela.firstPage();
  do {
    // checarBotoes();
    tela.fillScreen(GxEPD_WHITE);
    
    // --- Camada de Gráficos (Retângulos e Linhas) ---
    tela.drawRect(18, 9, 260, 110, GxEPD_BLACK);
    tela.drawRect(18, 29, 260, 45, GxEPD_BLACK);
    tela.drawRect(118, 9, 80, 110, GxEPD_BLACK);
    tela.drawLine(68, 29, 68, 72, GxEPD_BLACK);

    // --- Camada de Texto: Fonte BOLD (Agrupado) ---
    fontes.setFont(u8g2_font_helvB10_te);
    
    // Títulos
    fontes.setCursor(20, 22);  fontes.print("Luminosidade");
    fontes.setCursor(125, 22); fontes.print("Persiana");
    fontes.setCursor(205, 22); fontes.print("Lâmpada");
    
    // Valores Principais
    fontes.setCursor(30, 62);  fontes.print(strLuzAtual);
    fontes.setCursor(79, 62);  fontes.print(strMeta);
    fontes.setCursor(40, 106); fontes.print(fonte_atual);
    fontes.setCursor(145, 62); fontes.print(strPersiana);
    fontes.setCursor(122, 106); fontes.print(comandoPersiana);
    fontes.setCursor(223, 62); fontes.print(strLampada);
    fontes.setCursor(213, 106); fontes.print(cor_lampada);

    // --- Camada de Texto: Fonte REGULAR (Agrupado) ---
    fontes.setFont(u8g2_font_helvR10_te);
    
    // Labels
    fontes.setCursor(25, 42);  fontes.print("Atual");
    fontes.setCursor(76, 42);  fontes.print("Meta");
    fontes.setCursor(26, 86);  fontes.print("Fonte Luz");
    fontes.setCursor(130, 42); fontes.print("Abertura");
    fontes.setCursor(135, 86); fontes.print("Status");
    fontes.setCursor(198, 42); fontes.print("Intensidade");
    fontes.setCursor(224, 86); fontes.print("Cor");

  } while (tela.nextPage());
  
  tela.powerOff(); 
}

//Handlers dos botões
void upButtonPressed(GFButton& button) {
    Serial.println("Botão Físico: SUBIR");
    aplicarComandoPersiana(1); // 1 = Subir
}

void downButtonPressed(GFButton& button) {
    Serial.println("Botão Físico: DESCER");
    aplicarComandoPersiana(2); // 2 = Descer
}

void stopButtonPressed(GFButton& button) {
    Serial.println("Botão Físico: PARAR");
    aplicarComandoPersiana(0); // 0 = Parar
}


void reconectarWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin("Projeto", "2022-11-07");

        Serial.print("Conectando ao WiFi...");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(1000);
        }
    Serial.print("conectado!\nEndereço IP: ");
    Serial.println(WiFi.localIP());
    }
}

void reconectarMQTT() {
    if (!mqtt.connected()) {
        Serial.print("Conectando MQTT...");
        while(!mqtt.connected()) {
            mqtt.connect("PEXO", "aula", "zowmad-tavQez");
            Serial.print(".");
            delay(1000);
        }
        Serial.println(" conectado!");

        mqtt.subscribe("downlink/#");
    }
}

void ajustePersiana (const unsigned long code, int tempoDeMovimento) {
    // unsigned long instanteInicioAjuste = millis(); // Não precisa mais disso
    mySwitch.send(code, BITS);
    
    // O delay permite que o ESP32 gerencie WiFi e pinos em segundo plano
    delay(tempoDeMovimento); 
    
    mySwitch.send(STOP_CODE, BITS);
}

void recebeuMensagem(String topico, String conteudo) {
    Serial.println("\n" + topico + ": " + conteudo);
    if (topico.endsWith(String("downlink/ds/LED"))) {
        int valor = conteudo.toInt();
        Serial.println("Valor recebido: " + String(valor));
        if (valor == 1) {
            rgbLedWrite(RGB_BUILTIN, 0, 50, 0);
        }
        else {
            rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
        }
    }

   if (topico.endsWith(String("downlink/ds/Persiana"))) {
        int cmd = conteudo.toInt();
        Serial.println("Comando MQTT Recebido: " + String(cmd));
        
        // Agora o MQTT apenas chama a mesma função dos botões
        aplicarComandoPersiana(cmd);
    }

    if (topico.endsWith(String("downlink/ds/lightMeta"))) {
        metaNum = conteudo.toInt();
        preferencias.putInt("metaLum", metaNum);
        Serial.println("Nova meta de luminosidade: " + String(metaNum) + "%");
    }

    if (topico.endsWith(String("downlink/ds/blindStatus"))) {
        statusPersianaBlynk = conteudo.toInt();
        preferencias.putInt("statusPer", statusPersianaBlynk);
        Serial.println("Novo status da persiana via Blynk: " + String(statusPersianaBlynk) + "%");
        
        int diferenca = statusPersianaBlynk - statusPersiana;
        int tempoDeEspera = abs(diferenca) * 300;
        if (diferenca > 0) {//descer persiana
            ajustePersiana(DOWN_CODE, tempoDeEspera);
            statusPersiana = statusPersianaBlynk;
        }
        else if (diferenca < 0) {
            ajustePersiana(UP_CODE, tempoDeEspera);
            statusPersiana = statusPersianaBlynk;
        }
        else {
            Serial.println("Persiana já está na posição desejada.");
        }
    }

    if (topico.endsWith(String("downlink/ds/autoMode"))) {
        autoMode = conteudo.toInt();
        preferencias.putInt("autoMode", autoMode);
        Serial.println("Novo modo de operação recebido: " + String(autoMode));
        if (autoMode == 1) {
            Serial.println("Modo automático ativado.\n");
        }
        else {
            Serial.println("Modo automático desativado.\n");
        }
    }
}

void setup() {
    Serial.begin(115200); delay(500);
    SPI.end(); 
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    // -----------------------------------------------------

    pinMode(10, OUTPUT); // CS
    pinMode(14, OUTPUT); // REMOVER ESTA LINHA (A biblioteca controla o DC)
    pinMode(15, OUTPUT); // REMOVER ESTA LINHA (A biblioteca controla o RST)
    // pinMode(16, INPUT);  // REMOVER ESTA LINHA (A biblioteca controla o BUSY)
    
    // Opcional: Garante níveis iniciais seguros
    digitalWrite(10, HIGH);
    reconectarWiFi();
    conexaoSegura.setCACert(certificado1);

    mqtt.begin("mqtt.janks.dev.br", 8883, conexaoSegura);
    mqtt.onMessage(recebeuMensagem);
    mqtt.setKeepAlive(45);
    //mqtt.setWill("tópico da desconexão", "conteúdo");

    reconectarMQTT();

     
    
    
    mySwitch.enableTransmit(RF_PORT);
    
    button1.setPressHandler(upButtonPressed);
    button2.setPressHandler(downButtonPressed);
    button3.setPressHandler(stopButtonPressed);
    
    preferencias.begin("dadosPersiana");
    statusPersiana = preferencias.getInt("statusPer", 0);
    metaNum = preferencias.getInt("metaLum", -1);
    autoMode = preferencias.getInt("autoMode", 0);
    
    tela.init();
    tela.setRotation(1);
    tela.fillScreen(GxEPD_WHITE);
    tela.display(true);
    
    fontes.begin(tela);
    fontes.setForegroundColor(GxEPD_BLACK);
}

void loop() {
    reconectarWiFi();
    reconectarMQTT();
    mqtt.loop();
    checarBotoes();

    unsigned long instanteAtual = millis();
    if (instanteAtual > instanteAnterior + 3000) {  
        int leitura = analogRead(LDR_PIN);
        luminosidadeAtual = map(leitura, 0, 4095, 0, 100);
        Serial.print("Luminosidade atual: " + String(luminosidadeAtual) + "%\n");
        
        //Envio da Luminosidade via MQTT para o Blynk
        String conteudo = String(luminosidadeAtual);
        String conteudoLog = String(statusPersiana) + "/" + String(luminosidadeAtual) + "/" + String(metaNum);
        mqtt.publish("ds/LightSensor", conteudo);
        mqtt.publish("ds/log", conteudoLog);
        Serial.println(conteudoLog);
        if (metaNum == -1){//Não tem meta 
            Serial.println("Sem meta!");
        } 

        instanteAnterior = instanteAtual;
    }

     if ((instanteAtual > instanteAnterior2 + periodoDeChecagem*1000) && (metaNum != -1) && (autoMode == 1) && (estadoMovimentoAtual == 0)){
        Serial.println("\nVerificando necessidade de ajuste na persiana...");
        Serial.println("Status atual da persiana: " + String(statusPersiana) + "%");
        Serial.println("Luminosidade atual: " + String(luminosidadeAtual) + "%");
        Serial.println("Meta de luminosidade: " + String(metaNum) + "%\n");
        
        if ((luminosidadeAtual < (metaNum + 5)) && (luminosidadeAtual > (metaNum -5))){// Estamos na meta!
            //nao faz nada...
            Serial.println("\nEstamos na meta!");
        }

        else { // se está fora do treshold

            if (( (statusPersiana == 0) && (metaNum > luminosidadeAtual)) || ( (statusPersiana == 100) && (metaNum < luminosidadeAtual) ) ){//Nesse caso não da mais para fazer nada
                Serial.println("\nInfelizmente nao sera possivel atingir a meta, a persiana já está no limite. :(");
            }
            else{
                if (luminosidadeAtual < metaNum){//Aqui devemos subir a persiana.
                    //Se 30 segundos é 100% da persiana, queremos subir 5%. Logo precisamos subir a persiana por 1,5 segundos
                    novoStatus = statusPersiana - 5;
                    if (novoStatus < 0){
                        statusPersiana = 0;
                    }
                    else{
                        statusPersiana = novoStatus;
                        // mySwitch.send(UP_CODE, BITS);
                        // delay(2000);//dando 500 milisegundos de folga - testar melhor...
                        // mySwitch.send(STOP_CODE,BITS);
                        ajustePersiana(UP_CODE, 2000);
                    }
                    mqtt.publish("ds/blindStatus", String(statusPersiana));
                    preferencias.putInt("statusPer", statusPersiana);
                }
                
                else{
                    //Se 30 segundos é 100% da persiana, queremos descer 5%. Logo precisamos subir a persiana por 1,5 segundos
                    novoStatus = statusPersiana + 5;
                    if (novoStatus > 100){
                        statusPersiana = 100;
                    }
                    else{
                        statusPersiana = novoStatus;
                        // mySwitch.send(DOWN_CODE, BITS);
                        // delay(2000); //dando 500 milisegundos de folga - testar melhor...
                        // mySwitch.send(STOP_CODE,BITS);
                        ajustePersiana(DOWN_CODE, 2000);
                    }   
                    mqtt.publish("ds/blindStatus", String(statusPersiana));
                    preferencias.putInt("statusPer", statusPersiana);
                }
            }
            Serial.println("Novo status da persiana: " + String(statusPersiana) + "%\n");
        }
        instanteAnterior2 = instanteAtual;
    }

    if (instanteAtual > instanteAnterior3 + 30000) {  
        desenharTela();
        instanteAnterior3 = instanteAtual;
    }
}
