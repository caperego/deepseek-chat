# Deploy da API no ISPConfig — https://web.cap.im/deepseek

A API já foi aplicada no banco (`api_tokens` + usuário `deepseek_api`) e está
funcionando em teste na `.235` (`php -S 127.0.0.1:8099`).
Agora falta publicar dentro do site `web.cap.im`.

- **Site (ISPConfig):** `web.cap.im` → cliente `client1`, site `web98`
- **DocumentRoot:** `/var/www/clients/client1/web98/web`
- **Pasta da API:** `/var/www/clients/client1/web98/web/deepseek`
- **URL final:** `https://web.cap.im/deepseek`
- **Banco:** `deepseek_chat` @ `127.0.0.1`, usuário `deepseek_api`
- **Tabela de tokens:** `api_tokens`

Os arquivos já estão em `~/deepseek-api` no servidor (com `config.php` pronto).

---

## 0. Atualização (agente de arquivos / permissões)

O `/chat/stream` agora repassa `tools`/`tool_choice` para a API DeepSeek e não
duplica mais a mensagem do usuário quando o cliente reenvia o histórico com
resultados de ferramentas. Os projetos ganharam o campo `permission_mode`
(`ask` | `auto` | `full`).

Aplique a migração e republica os arquivos:

```bash
sudo mariadb deepseek_chat -e \
  "ALTER TABLE projects ADD COLUMN IF NOT EXISTS permission_mode VARCHAR(16) NOT NULL DEFAULT 'ask';"

sudo cp -r /home/caperego/deepseek-api/. /var/www/clients/client1/web98/web/deepseek/
sudo chown -R web98:client1 /var/www/clients/client1/web98/web/deepseek
sudo find /var/www/clients/client1/web98/web/deepseek -type f -exec chmod 644 {} \;
```

---

## 1. Publicar (opção A — sudo no servidor)

No `.235`:

```bash
sudo mkdir -p /var/www/clients/client1/web98/web/deepseek
sudo cp -r /home/caperego/deepseek-api/. /var/www/clients/client1/web98/web/deepseek/
sudo chown -R web98:client1 /var/www/clients/client1/web98/web/deepseek
sudo find /var/www/clients/client1/web98/web/deepseek -type f -exec chmod 644 {} \;
sudo find /var/www/clients/client1/web98/web/deepseek -type d -exec chmod 755 {} \;
```

### Opção B — ISPConfig File Manager / SFTP

1. ISPConfig → **Sites → web.cap.im → File Manager** (ou SFTP do site).
2. Entre em `/web` e crie a pasta `deepseek`.
3. Envie o conteúdo de `~/deepseek-api` (ou da pasta local `deepseek-api/`) para
   `/web/deepseek`.
4. Garanta que o `config.php` esteja presente e que o `.htaccess`/`.user.ini`
   também foram enviados (arquivos ocultos).

---

## 2. config.php

```php
<?php
declare(strict_types=1);

return [
    'db' => [
        'host' => '127.0.0.1',
        'port' => 3306,
        'name' => 'deepseek_chat',
        'user' => 'deepseek_api',
        'password' => 'SENHA_DEFINIDA_NO_DEPLOY',
    ],
    'deepseek' => [
        'api_key' => 'sk-...',
        'base_url' => 'https://api.deepseek.com',
    ],
    'tokens' => ['ttl_days' => 30],
];
```

> Para trocar a senha depois: `ALTER USER 'deepseek_api'@'localhost' IDENTIFIED BY
> 'NOVA_SENHA'; FLUSH PRIVILEGES;` e atualizar o `password` no `config.php`.

---

## 3. Verificar

```bash
curl -s https://web.cap.im/deepseek/health
# {"ok":true,"time":"..."}
```

Login e uso:

```bash
TOKEN=$(curl -s -X POST https://web.cap.im/deepseek/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"caperego","password":"SUA_SENHA"}' | php -r 'echo json_decode(stream_get_contents(STDIN),true)["token"];')

curl -s https://web.cap.im/deepseek/models -H "Authorization: Bearer $TOKEN"
```

---

## 4. Endpoints

| Método | Rota | Descrição |
|---|---|---|
| POST | `/auth/register` | Cria conta (`username`, `display_name`, `password`) |
| POST | `/auth/login` | Retorna `token` (30 dias) |
| POST | `/auth/logout` | Revoga o token atual |
| GET | `/me` | Usuário autenticado |
| GET | `/models` | Proxy da lista de modelos DeepSeek |
| POST | `/chat/stream` | **SSE** do chat; salva mensagens e tokens |
| GET/POST | `/projects` | Lista/cria projetos |
| PUT/DELETE | `/projects/{id}` | Atualiza/exclui |
| GET/POST | `/conversations` | Lista (`?project_id=`) / cria |
| PUT/DELETE | `/conversations/{id}` | Renomeia/atualiza/exclui |
| GET/POST | `/messages` | Lista (`?conversation_id=`) / cria |
| GET/POST | `/commands` | Globais ou do projeto (`?project_id=`) |
| PUT/DELETE | `/commands/{id}` | Atualiza/exclui |
| GET/POST | `/project-items` | Arquivos do projeto |
| DELETE | `/project-items/{id}` | Remove item |
| GET | `/activities` | Registro de atividades |
| POST | `/activities` | Registra evento do cliente (`file_write`, `attachment_add`, `project_attachment`, `command_use`, `dictation`) |
| GET | `/usage` | Tokens (conversa e por modelo) |

Formato do `/chat/stream` (requisição):

```json
{
  "conversation_id": 0,
  "project_id": 0,
  "title": "primeira mensagem...",
  "model": "deepseek-flash",
  "effort": "high",
  "messages": [
    {"role": "system", "content": "..."},
    {"role": "user", "content": "Oi"}
  ]
}
```

A resposta é `text/event-stream`. O primeiro evento é
`event: meta` com `{"conversation_id": N}` (útil quando a conversa é nova).

---

## 5. Observações importantes

- **Streaming**: o `.user.ini` desliga o buffer
  (`output_buffering=Off`, `zlib.output_compression=Off`). Se usar nginx na
  frente, o cabeçalho `X-Accel-Buffering: no` já é enviado.
- **config.php** é bloqueado pelo `.htaccess` (`Require all denied`).
- **HTTPS**: ative o Let's Encrypt no ISPConfig para `web.cap.im`.
- **Segurança**: o backend guarda a API key do DeepSeek e o acesso ao banco;
  o aplicativo nunca vê essas credenciais (só o token).
- **Usuários antigos**: senhas em PBKDF2 (do app Qt) são validadas e
  regravadas em bcrypt automaticamente no primeiro login.
