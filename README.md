# TRABALHO MQTT.

## Dependencias

Para execução é necessário que a biblioteca PAHO MQTT esteja instalada na máquina. Para esta aplicação foi utilizado a biblioteca ncurses. Ela vem disponivel na maioria das distribuições Linux. 

## Broker

A nossa aplicação necessita de um broker MQTT disponível para comunicação. No arquivo `settings.h` é definido o endereço do Broker. Por padrão ele utiliza o localhost, então se desejar utilizar outro se faz necessário modificar o endereço no arquivo.

## Execução do programa

Para compilar o programa dentro da pasta do código fonte rode o comando:

```shell
make
```
Ele irá criar um binário chamado "zaperson" (nome da aplicação). Para execução adicione os parâmetros necessários:

```shell
./zaperson id_do_cliente lista_de_grupos
```

O id do cliente é como o usuário vai se identificar no programa. Já a lista de grupos trata dos grupos que o usuário gostaria de se conectar.

## Interação com o programa

O usuário poderá interagir com o programa por meio da interface gráfica gerada pelo ncurses. Ao executar o programa o usuário estará na tela inicial, onde irá ser apresentado aos chats disponíveis. O input da tela inicial é onde o usuário deverá adicionar o id dos usuários que ele deseja se comunicar. Com as setas de navegação o usuário poderá selecionar em qual chat deseja entrar. Quando dentro de uma conversa poderá digitar uma mensagem no input e as mensagens enviadas e recebidas serão impressas na interface.