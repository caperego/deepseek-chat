# Revisão 1.2.0

## Referência e escopo

Referência consultada: [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness),
commit `687f282e8625c6b8d11ff58211a90524be2e4317`, obtido com clone local.
Foram examinados o painel `ui-settings-general`, as linhas de aparência e
tipografia de `ui-theme`, o layout e a documentação de plugins e tarefas.
Este registro cobre os componentes e cenários abaixo; não afirma uma auditoria
de todas as linhas dos dois repositórios nem paridade completa com o Harness.

O Harness usa uma arquitetura de plugins Cordis em TypeScript. Este projeto usa
Qt/QML e backend PHP: bundles Cordis não podem ser instalados como plugins MCP.
As interfaces foram adaptadas aos recursos do cliente, mantendo a identidade
DeepSeek Chat e os modelos configurados no backend existente.

## Mudanças

- Painel de configurações de até 800 × 800, navegação lateral de 188 px,
  rolagem independente, cartões de tema claro/escuro/sistema e cantos arredondados.
- Campos, seletores, menus dos seletores, caixas de seleção, números, abas e
  diálogos compartilham componentes usados também em login, projetos e arquivos.
- Barra lateral com ícones, divisórias, conversas recuadas, plugins e automações.
- Plugins MCP podem ser adicionados e ativados/desativados pela interface.
  O JSON existente é preservado; erro de sintaxe impede sobrescrita pelo editor.
- Chamadas MCP são correlacionadas com servidor e identificador. Saída, erros,
  timeout e encerramento do processo devolvem resultado à execução correta.
  Parar uma execução remove referências pendentes; respostas tardias são ignoradas.
- Streaming agrupa notificações do modelo a cada 33 ms, mantendo o texto integral
  e a atualização final imediata. Um lote de 1.000 fragmentos é coberto por teste
  que verifica uma única notificação e nenhum conteúdo perdido.
- `run_command` drena o processo enquanto ele roda e retém até 100 KiB de bytes
  mais o marcador de truncamento, com início e final. Não acumula toda a saída
  em memória para depois cortá-la. O tamanho em caracteres/JSON pode diferir por
  causa da decodificação e do escape.
- Gravações de arquivos são atômicas; links simbólicos são resolvidos nas
  verificações de permissão. Comandos perigosos conhecidos exigem confirmação
  mesmo com permissões persistentes ou de sessão.
- Atualização Linux substitui o executável por renomeação, permitindo instalar
  enquanto uma versão anterior continua aberta.

## Automações

Em **Automações**, informe nome, instrução, projeto, atraso inicial em minutos
e repetição: uma vez, a cada hora, 24 horas ou 7 dias. O modelo e esforço atuais
são registrados no agendamento. A tarefa executa em conversa separada, com modo
de permissão `ask`, sem alterar o chat aberto ou seus anexos.
Permissões persistentes do projeto e aprovações dadas durante a execução
continuam aplicáveis; os comandos perigosos conhecidos sempre exigem confirmação.

- O aplicativo deve estar aberto e autenticado. Fechá-lo totalmente interrompe
  o agendador; a bandeja mantém o processo aberto quando essa opção está ativa.
- Os intervalos são durações transcorridas. “A cada 24 horas” não é uma regra de
  horário civil ajustada automaticamente ao horário de verão.
- A verificação ocorre a cada 15 segundos. Tarefas vencidas podem atrasar enquanto
  houver três execuções ativas. Intervalos perdidos são reunidos em uma execução.
- Uma tarefa em execução não se sobrepõe a si mesma. O disparo é gravado antes
  de iniciar a requisição. Após uma interrupção, tarefas já disparadas não são
  executadas novamente automaticamente; uma tarefa única pode ser reativada.
- O armazenamento é local, separado por backend e usuário. Trocar a URL exige
  novo login antes de executar ou criar automações nesse backend.
- Instruções, estado e última conversa ficam em `AppDataLocation/automations/`,
  em JSON com permissões restritas. O histórico das conversas continua no backend.
- Pausar impede os próximos disparos. **Parar** cancela a execução ativa;
  cancelar uma chamada não garante que um processo externo reverta efeitos já feitos.

## Validação reproduzível

```bash
bash deepseek-chat/build.sh
qmake6 -o deepseek-chat/build/linux-regression/Makefile deepseek-chat/tests/agent-regression.pro
make -C deepseek-chat/build/linux-regression -j4
QT_QPA_PLATFORM=offscreen deepseek-chat/build/linux-regression/agent-regression
```

Os testes usam servidor HTTP local e servidores MCP fictícios, sem conta ou API
externa. Cobrem streaming, permissões, gravação, saída limitada, MCP concorrente,
encerramento, timeout, cancelamento, agendamentos persistidos, retomada e execução
agendada sem substituir a conversa aberta. Python 3 é usado nos testes MCP.
Na validação Linux com Qt 6.11.2: **31 testes passaram**, sem falhas ou casos
ignorados. O backend opcional de voz `speechd` não estava disponível nesse ambiente.

As capturas do README são geradas dos próprios recursos QML, com dados fictícios:

```bash
qmake6 -o deepseek-chat/build/ui-preview/Makefile deepseek-chat/tests/ui-preview.pro
make -C deepseek-chat/build/ui-preview -j4
QT_QPA_PLATFORM=offscreen deepseek-chat/build/ui-preview/ui-preview "$PWD/docs/images"
```

O modo `--qa` gera capturas das sete seções de configurações, automações e um
diálogo secundário, em claro e escuro, com janela de 920 × 600 e fonte de 14 pt:

```bash
QT_QPA_PLATFORM=offscreen deepseek-chat/build/ui-preview/ui-preview /tmp/deepseek-layout-qa --qa
```

As imagens demonstram layout e controles; não simulam uma conexão real de plugin
nem são evidência de execução de tarefas. Os testes de integração cobrem execução.

## Limites

Não há sandbox de sistema operacional. Os controles de permissão não isolam um
comando ou plugin já autorizado do restante do computador. O catálogo de modelos
depende da API configurada. Prévia HTML não executa JavaScript. O pacote Linux
usa Qt/glibc do sistema e deve ser compilado em uma distribuição compatível.
Os cenários de teste não equivalem a um benchmark comparativo contra o Harness.
