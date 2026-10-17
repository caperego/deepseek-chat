# API do backend (`deepseek-api`)

Base: `https://SEU_DOMINIO/deepseek` — todas as rotas (exceto login/cadastro/health)
exigem `Authorization: Bearer <token>`.

## Autenticação

| Método | Rota | Descrição |
|---|---|---|
| GET | `/health` | `{"ok":true,"time":...}` |
| POST | `/auth/register` | `{username, display_name, password}` |
| POST | `/auth/login` | `{username, password}` → `{token, user}` |
| POST | `/auth/logout` | revoga o token atual |
| GET | `/me` | usuário autenticado |
| PUT | `/me` | `{display_name}` |
| PUT | `/me/password` | `{old_password, new_password}` |

## Modelos, saldo e chat

| Método | Rota | Descrição |
|---|---|---|
| GET | `/models` | lista de modelos (agrega provedores configurados) |
| GET | `/balance` | saldo na DeepSeek (`is_available`, `balance_infos`) |
| POST | `/chat/stream` | **SSE** do chat (ver abaixo) |

### Corpo do `/chat/stream`

```json
{
  "conversation_id": 0,
  "project_id": 0,
  "title": "primeira mensagem...",
  "model": "deepseek-flash",          // ou "provedor/modelo"
  "effort": "high",                   // só enviado ao DeepSeek
  "messages": [
    {"role": "system",    "content": "..."},
    {"role": "user",      "content": "Oi"},
    {"role": "assistant", "content": "", "tool_calls": [{"id":"call_1","type":"function",
      "function":{"name":"list_files","arguments":"{}"}}]},
    {"role": "tool", "tool_call_id": "call_1", "content": "{\"entries\":[...]}"}
  ],
  "tools": [ ... definições OpenAI-compatible ... ],
  "tool_choice": "auto",
  "attachments": [ {"kind":"file","name":"...","path":"...","mime":"...","text":"..."} ],
  "sources": [ {"url":"https://...","title":"...","excerpt":"..."} ],
  "persist": true,
  "persist_user": true
}
```

- `persist:false` → não grava nada (usado para títulos/resumos internos).
- `persist_user:false` → não grava a última mensagem do usuário (regenerar).
- `sources` registra até 12 páginas efetivamente abertas para exibir junto à resposta.
- A resposta é `text/event-stream`; o primeiro evento é
  `event: meta` com `{"conversation_id": N}`; os deltas seguem o formato da API
  DeepSeek (`choices[0].delta.content`, `reasoning_content`, `tool_calls`) e há um
  bloco final com `usage`.

## Projetos, conversas e arquivos

| Método | Rota | Descrição |
|---|---|---|
| GET/POST | `/projects` | lista/cria (inclui `permission_mode`) |
| PUT/DELETE | `/projects/{id}` | atualiza/exclui |
| GET/POST | `/conversations` | lista (`?project_id=0` sem projeto) / cria |
| PUT/DELETE | `/conversations/{id}` | renomeia/move (`project_id`)/exclui |
| POST | `/conversations/{id}/truncate` | apaga mensagens a partir de `from_message_id` |
| POST | `/conversations/{id}/branch` | cria ramificação; `through_message_id` inclui até a mensagem indicada, `0` copia tudo, `-1` começa vazia |
| GET/POST | `/project-memories` | lista ou cria memórias do projeto (`project_id`) |
| PUT/DELETE | `/project-memories/{id}` | edita ou exclui memória |
| GET/POST | `/messages` | histórico (`?conversation_id=`) / grava |
| GET/POST | `/commands` | comandos globais ou do projeto (`?project_id=`) |
| PUT/DELETE | `/commands/{id}` | atualiza/exclui |
| GET/POST | `/project-items` | arquivos do projeto (metadados) |
| DELETE | `/project-items/{id}` | remove item |
| GET | `/search?q=` | busca no conteúdo das mensagens |
| GET | `/activities` | auditoria |
| POST | `/activities` | registra evento do cliente |
| GET | `/usage?conversation_id=` | tokens por conversa e por modelo |

As mensagens retornadas por `/messages` incluem `sources` quando a resposta usou
páginas da web. Cada fonte contém `url`, `title` e `excerpt`.

Instalações existentes precisam aplicar `deepseek-api/sql/2026-09-28-chat-features.sql`
antes de usar os novos endpoints.

## Multi-provedor

Em `config.php`, além de `deepseek`, é possível declarar `providers`:

```php
'providers' => [
    'local' => [
        'base_url' => 'http://127.0.0.1:11434/v1',
        'api_key'  => 'ollama',
        'effort'   => false,
        'images'   => false,
        'context_window' => 32000,
    ],
],
```

Os modelos aparecem como `local/<modelo>` na lista e o roteamento é automático.
O parâmetro `effort` só é repassado quando `effort => true` (DeepSeek).
