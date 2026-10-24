# Verificação da interface — 29/09/2026

Aplicativo Qt executado no Windows a partir de `deepseek-chat/dist/deepseek-chat.exe`, conectado à API configurada. Testes interativos feitos com Computer Use depois de autorizar o executável.

## Fluxos verificados

| Fluxo | Resultado observado |
| --- | --- |
| Composição | Shift+Enter inseriu uma quebra de linha; Enter enviou a mensagem; campo vazio manteve Enviar desativado. |
| DeepSeek | Recebeu e exibiu respostas reais do modelo configurado. |
| Histórico | A conversa e sua ramificação foram reabertas depois de reiniciar o aplicativo. |
| Pesquisa | Localizou títulos e mensagens da conversa de teste. |
| Ramificação | Criou uma conversa com as mensagens anteriores; Ver original retornou ao histórico preservado. |
| Projeto | Criou o projeto identificado como teste, com uma pasta isolada dentro de build. |
| Memória | Salvou o fato fictício Aurora; uma conversa nova respondeu Aurora sem esse nome estar na pergunta. A memória continuou disponível após reiniciar o aplicativo. |
| Web | Consultou duas páginas oficiais do Qt; a resposta exibiu os links e os cartões de fontes. Os cartões continuaram disponíveis ao reabrir o histórico. |
| Artefatos | Abriu preview.md pelo botão Painel, editou o texto, exibiu diferenças e prévia Markdown, salvou e restaurou a versão anterior. O arquivo restaurado foi conferido no disco. |
| Captura | Capturou a janela do próprio aplicativo e mostrou a miniatura como anexo no compositor. A imagem não foi enviada ao modelo. |

## Correções feitas durante os testes

- Tabelas Markdown passaram a exibir cabeçalhos e células, com suporte a alinhamento e barras escapadas.
- Blocos de código passaram a usar Consolas, fundo uniforme e cores com contraste adequado no tema claro.
- Diálogos da barra lateral e de mensagens foram centralizados na janela.
- Botões de web, voz e comandos receberam nomes acessíveis e dicas.
- A lista de arquivos ganhou o botão Painel para acessar edição, prévia, diferenças e versões.

O executável foi recompilado, copiado para dist e reaberto. Os arquivos QML alterados passaram pelo qmllint sem erros; git diff --check também passou. A formatação foi inspecionada adicionalmente em uma renderização local com largura de 920 pixels.

## Dados de teste e limites

- Projeto mantido para inspeção: **Teste UI DeepSeek 2026-09-29**.
- Pasta de teste: `deepseek-chat/build/ui-sandbox`; arquivo `preview.md` e versões de segurança.
- Conversa independente de formatação e sua ramificação também foram mantidas.
- Gravação de voz e seleção de uma região da tela não foram verificadas nesta sessão interativa.
- Configuração de modelos e provedores não foi alterada.

## Correção de tarefas interrompidas — 2026-09-30 (1.1.9)

O relato e a imagem mostravam várias etapas terminando em uma promessa de continuar.
O código tinha um limite de 12 rodadas sem uma conclusão dedicada, descartava o
`reasoning_content` das chamadas seguintes e podia tratar erros do stream como sucesso.
A janela encontrada inicialmente usava o executável da outra pasta, `DeepSeekDesktop`.

- As 12 rodadas com ferramentas agora são seguidas por uma rodada de conclusão,
  sem novas ferramentas e com instrução para informar resultados e pendências.
- O contexto de raciocínio das mensagens assistant é reenviado quando há ferramentas,
  conforme a documentação do DeepSeek: https://api-docs.deepseek.com/guides/thinking_mode/.
- Erros SSE/JSON, conexão interrompida, resposta sem conclusão e limite de saída
  geram um erro visível; chamadas de ferramentas incompletas não são executadas.
- O trecho recebido é mantido na interface e no contexto local para continuação explícita.
- Parar enquanto a execução aguarda autorização libera o estado de processamento.
- Comandos Windows usam argumentos nativos do cmd.exe para preservar aspas;
  a saída com acentos usa a página de códigos OEM quando não é UTF-8.

### Regressões automatizadas

`deepseek-chat/tests/agent-regression.pro` e `agent-regression.cpp` usam a implementação
real compilada, um servidor HTTP local e uma pasta temporária. Não usam credenciais
nem alteram o banco remoto. Compilar o aplicativo antes de gerar o projeto dos testes.

Resultado: **16 testes aprovados, zero falhas**. Incluem sucesso e falhas do stream,
perda de conexão após texto parcial, erro HTTP e erro JSON dentro do proxy SSE,
limite de saída, raciocínio sem resposta, última linha sem newline, ferramentas válidas
e incompletas, 12 ações seguidas de conclusão, contexto reenviado, cancelamento
durante autorização e comando Windows com caminho entre aspas e saída com acentos.

O executável 1.1.9 foi compilado e copiado de `bin` para `dist`, com hashes iguais.

### Verificação com o servidor real

O aplicativo atualizado foi aberto pelo caminho completo de `DeepSeekDesktop-GPT`.
No projeto isolado **Teste UI DeepSeek 2026-09-29**, a conversa
**Teste de conclusão com preview.md** pediu listagem da pasta e leitura do arquivo,
sem gravações nem comandos. O DeepSeek configurado respondeu com a conclusão,
nome do arquivo, tamanho, número de linhas e conteúdo conferido no disco.
O botão voltou para Enviar e a resposta final permaneceu no histórico recarregado.
O pedido completo da calculadora não foi repetido nesta verificação; o fluxo de
comandos com aspas foi validado pelo teste local que executa o controller real.

## Status simples na conversa — 2026-09-30 (1.1.10)

- Nomes internos das ferramentas foram substituídos na apresentação por status
  como Pensando…, Processando…, Executando…, Pesquisando… e Lendo arquivo….
- A saída dos comandos e o nome técnico ficam em Ver detalhes, recolhido por padrão.
- O raciocínio também fica recolhido, com opção de consultar os detalhes.
- Blocos DSML retornados pelo modelo como texto ficam em detalhes, com aviso
  de que a etapa não tem execução confirmada; eles não são executados pela interface.
- Etapas negadas, canceladas ou com falha mostram o estado correspondente.
- Conferência visual com renderização local do QML real: status de execução,
  pesquisa e pensamento apareceram sem textos internos abertos.
- Executável recompilado, copiado para dist e reaberto; hashes de bin e dist iguais.

## Aparência e navegação ajustável — 2026-09-30 (1.1.11)

- Configurações → Aparência separa os tamanhos de menus/diálogos, projetos/conversas,
  mensagens/escrita e código/saídas. A família das mensagens pode ser diferente da interface.
- A tipografia da lista lateral passou a respeitar seu controle próprio; tamanhos
  em pixels fixos foram substituídos por pontos nessa lista.
- Barra lateral redimensionável pela divisória, recolhível por ‹ e reaberta por ☰.
  A largura e o estado recolhido são salvos no config.ini. Duplo clique na divisória
  restaura a largura padrão. As setas dos projetos recolhem suas conversas na sessão.
- Seleção de conversa sem brilho, botões laterais discretos, pesquisa com cantos
  arredondados, compositor centralizado e dica de teclado fora do campo de escrita.
- Blocos de código, código inline e saídas usam o tamanho próprio de código.

### Verificação

Renderizações em 1280 e 920 pixels passaram pela conferência visual. Na prévia QML
real, aumentar o chat para 24 pt alterou o compositor, mantendo a lista lateral,
a fonte da interface e o código em 12 pt. Os atalhos de envio/quebra de linha
continuaram funcionando. O qmllint não apontou erros e git diff --check passou.

No executável conectado ao servidor, o arraste alterou a largura de 284 para 352;
o botão recolheu a navegação. Após reiniciar, a barra continuou recolhida e reabriu
na largura salva. A seta de um projeto ocultou apenas suas conversas. A tela de
aparência mostrou os quatro controles e as famílias de fonte carregadas.

Executável compilado e atualizado em dist; hashes de bin e dist conferidos.
Modelos e provedores foram preservados.

## Abertura direta — 2026-09-30 (1.1.12)

- Removida a janela de apresentação com logo (QSplashScreen).
- A recuperação da sessão começa depois do primeiro quadro da janela principal.
  Durante a conexão, a interface exibe “Carregando suas conversas…” e aguarda
  a conclusão antes de habilitar as ações. Sem sessão salva, mostra o login.
- Compilação concluída e executável de dist atualizado, com hash igual ao de bin.
- Na abertura real, a janela principal apareceu durante o carregamento, sem
  apresentação separada. Em seguida, a sessão foi recuperada, projetos e conversas
  carregaram, o aviso sumiu e os controles ficaram disponíveis.
- qmllint não apontou erros; git diff --check passou.

## Painel de arquivos — 1.2.1

A divisória entre o chat e o arquivo aceita arrastar; um clique duplo expande
para a janela. O botão Expandir/Reduzir alterna entre o painel lateral e a
visualização ampliada. A edição permanece no mesmo componente, preservando
alterações não salvas. Fechar o arquivo restaura a navegação e a conversa.

Teste com dados fictícios, sem conta ou API externa:

```bash
bash deepseek-chat/build.sh
qmake6 -o deepseek-chat/build/ui-preview/Makefile deepseek-chat/tests/ui-preview.pro
make -C deepseek-chat/build/ui-preview -j4
QT_QPA_PLATFORM=offscreen deepseek-chat/build/ui-preview/ui-preview /tmp/deepseek-panel-qa --artifact
```

O teste arrasta a divisória com eventos de mouse, clica em Expandir e Reduzir,
verifica a preservação de uma edição não salva, redimensiona para 920 × 600 e
confere o retorno à conversa ao fechar o arquivo expandido. As capturas são
salvas na pasta informada, com conteúdo fictício.


## Continuidade de tarefas — 1.2.1

A conclusão forçada após 12 rodadas foi removida. O cliente mantém ferramentas
por até 128 rodadas por execução; ao atingir esse limite, mostra uma pausa
explícita e preserva o contexto local de chamadas e resultados para o próximo
pedido. Esse limite evita ciclos indefinidos e não comprova conclusão.

Depois de utilizar ferramentas, cada resposta candidata a final passa por uma
revisão automática de pendências. A revisão pode voltar a executar ferramentas;
se responder apenas com texto novamente, o aplicativo encerra. A revisão é uma
instrução ao modelo, não uma garantia de que o trabalho esteja correto ou completo.
Ela preserva o modo de permissões e acrescenta uma chamada ao modelo em cada
encerramento candidato; essa chamada pode consumir tokens adicionais.

Cortes explícitos por `finish_reason=length`, com texto e sem chamadas de
ferramentas, permitem até três continuações automáticas. Falhas de rede, erros
HTTP/SSE e ferramentas incompletas não são repetidos automaticamente. O botão
Parar e as aprovações continuam ativos. Após falha, o transcript local completo
é mantido na conversa atual; não se promete recuperação desse contexto após
fechar o aplicativo, pois o servidor não persiste os resultados das ferramentas.

Regressões com servidor local: mais de 12 rodadas mantendo ferramentas e
raciocínio; duas retomadas de trabalho após respostas intermediárias; parada
após 128 rodadas e envio do contexto em ‘continuar’; corte de resposta com
limite de três retomadas; queda de conexão sem repetição de ferramentas.

Resultado desta versão: **37 testes aprovados, zero falhas** no Linux, além do
teste de interação do painel de arquivos. Compilação e instalação local
verificadas; o instalador preservou `config.ini` e `mcp.json`, e o executável
instalado corresponde ao compilado por SHA-256. O ambiente de testes não
dispõe do backend de voz opcional `speechd`; voz não foi validada nesta revisão.


## Correção de chamadas como texto e perda de contexto — 1.2.2

O aviso “Esta etapa não tem execução confirmada” era acionado na interface
quando o conteúdo continha marcação DSML. O cliente não interpretava isso como
falha de protocolo, então uma tentativa de usar ferramenta podia encerrar como
resposta normal. A revisão genérica da 1.2.1 gerava mais uma chamada sem corrigir
o formato. Essa revisão foi removida.

A versão 1.2.2 detecta envelopes de chamada DSML fora de blocos de código,
considera a resposta inválida e solicita até dois reenvios com ferramentas
estruturadas. O texto inválido nunca é convertido em comando. Chamadas nativas
válidas continuam passando pelas mesmas aprovações; se o provedor não reenviar
uma chamada válida, a execução termina com erro explícito. Não há novas
repetições automáticas em falhas de rede. Uma chamada válida que falhe ao
executar também não é repetida por esse mecanismo.

Mensagens antigas de assistant com essa marcação são higienizadas apenas no
pedido enviado ao modelo, sem alterar registros no servidor. Exemplos em
blocos de código permanecem literais. A interface usa o mesmo critério e
explica que o trecho inválido não foi executado.

Outra causa de perda de contexto foi confirmada: ao finalizar uma execução,
o cliente substituía seu histórico completo pelo retorno do banco, que contém
texto e não contém resultados de ferramentas. Agora os resultados locais e
suas chamadas são mantidos enquanto a conversa permanece aberta; os IDs das
mensagens são conciliados com o banco sem descartar o contexto. Não se promete
persistência desses resultados ao fechar/reabrir a conversa ou o aplicativo.
A seleção de histórico com resumo também evita cortar uma chamada de seu
resultado ao escolher a cauda da conversa.

A API oficial define as ações no campo `tool_calls`; marcação em `content`
não é evidência de execução. Referência:
https://api-docs.deepseek.com/guides/tool_calls/.
Há relatos semelhantes no projeto de referência, sem comprovação de que a
causa do provedor seja idêntica em todos os casos:
https://github.com/deepseek-ai/deepseek-harness/discussions/10100.

Novos testes usam um servidor HTTP local e arquivos reais temporários:
reenvio de três variantes de DSML seguido de leitura de `site.html` por caminho
relativo à pasta de trabalho; falha persistente limitada; promessa textual após
reenvio inválido não tratada como sucesso; exemplos literais; higienização de
histórico; preservação de resultados após retorno de banco sem ferramentas;
encerramento normal sem revisão extra. Nenhuma conta ou chave real é usada.

Resultado: **46 testes aprovados, zero falhas** no Linux, mais o teste de
interação do painel. Compilação, pacote e instalação local verificados por
SHA-256; `config.ini` e `mcp.json` foram preservados. O teste de reprodução
usa respostas controladas de servidor local, não valida o comportamento do
provedor real na conversa do usuário. Voz opcional (`speechd`) indisponível
no ambiente de testes.


## Painel de tarefas, navegador e terminal — 1.3.0

O painel fica abaixo da conversa, tem divisória ajustável e modo ampliado.
A navegação lateral e a paleta de comandos abrem cada uma das três abas.

Tarefas locais usam processos independentes, até quatro simultâneos e trinta
registros por sessão, com saída limitada a 200 KiB (início e fim). Há drenagem
contínua dos pipes e atualizações de interface agrupadas em 100 ms. No Linux,
cancelamento e encerramento matam o grupo do processo; processos que se
separem deliberadamente do grupo não são gerenciados por esse mecanismo.
Limpar concluídas não cancela tarefas ativas. Logout/Sair encerra os processos.
Iniciar uma tarefa é diferente de concluir; a IA recebe ID, estado e saída.
As ferramentas são `start_background_task`, `background_task_status` e
`stop_background_task`. Iniciar reaproveita a política de aprovação de comandos;
comandos perigosos continuam exigindo aprovação mesmo no modo completo.

O navegador usa Qt WebEngine Quick com perfil temporário isolado e só é
instanciado na primeira abertura da aba. Navegação HTTP/HTTPS, JavaScript,
histórico, recarga e abertura externa. Não há WebChannel entre páginas e a API
do aplicativo. Downloads são encaminhados ao navegador externo por ação do
usuário. Logout destrói a página/perfil local. Não foi adicionada automação
visual do navegador pela IA; `browse_url` mantém seu comportamento anterior.
Referência de inicialização: https://doc.qt.io/qt-6/qtwebenginequick.html.

O terminal existente foi movido do diálogo modal para o painel. A PTY inicia
na pasta do projeto (ou pasta pessoal), define TERM e tamanho inicial, usa
Bash sem arquivos de perfil/rc (ou sh, se Bash não existir), sem temas de prompt e aceita
entrada e Ctrl+C e mantém a sessão ao recolher. Decodificação UTF-8 incremental
e filtragem de sequências ANSI/OSC evitam códigos de controle visíveis. A saída
é um transcript linear limitado a 200 mil caracteres, com atualização em 33 ms,
e não um emulador completo de programas de tela inteira. ConPTY no Windows não
foi validado. O backend de voz opcional `speechd` permanece indisponível no teste.

A prova de interface `--workspace` inicia uma tarefa real, recolhe o painel e
verifica que continua; executa printf na PTY e confere a saída efetiva (não só
o eco do comando); carrega HTTP de um servidor local e clica por JavaScript em
um botão, conferindo a mudança de título. As imagens contêm dados fictícios.

Resultado final: **52 testes aprovados, zero falhas**, além das provas de
interface `--workspace` e `--artifact`. As dependências de link dos projetos
de teste foram corrigidas para reconstruir os executáveis de teste quando
qualquer objeto do aplicativo muda, evitando validar uma implementação antiga.
Capturas revisadas visualmente e publicadas com dados fictícios. Qt WebEngine
foi testado com `--disable-gpu` apenas na prova offscreen; o aplicativo mantém
as configurações padrão do Chromium e seu sandbox.

Instalador 1.3.0 executado no Linux: configuração e MCP preservados, executável
instalado idêntico ao compilado por SHA-256.
