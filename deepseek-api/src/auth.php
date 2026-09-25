<?php
declare(strict_types=1);

function pbkdf2_hash(string $password, string $saltHex): string
{
    $salt = hex2bin($saltHex) ?: '';
    return bin2hex(hash_pbkdf2('sha256', $password, $salt, 120000, 32, true));
}

function auth_register(Db $db, array $body): never
{
    $username = trim((string)($body['username'] ?? ''));
    $displayName = trim((string)($body['display_name'] ?? ''));
    $password = (string)($body['password'] ?? '');

    if (mb_strlen($username) < 3) {
        fail(422, 'O usuário precisa ter pelo menos 3 caracteres.');
    }
    if (strlen($password) < 4) {
        fail(422, 'A senha precisa ter pelo menos 4 caracteres.');
    }
    if ($db->one('SELECT id FROM users WHERE username = ?', [$username])) {
        fail(409, 'Este usuário já existe.');
    }

    $hash = password_hash($password, PASSWORD_BCRYPT);
    $salt = bin2hex(random_bytes(16));
    $name = $displayName !== '' ? $displayName : $username;

    $db->q(
        'INSERT INTO users (username, display_name, password_hash, salt) VALUES (?, ?, ?, ?)',
        [$username, $name, $hash, $salt]
    );
    $userId = $db->id();
    $db->q(
        'INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
        [$userId, 'register', 'Conta criada para ' . $username]
    );

    json_out(['id' => $userId, 'username' => $username, 'display_name' => $name], 201);
}

function auth_verify_password(array $user, string $password): bool
{
    $stored = (string)$user['password_hash'];
    if (str_starts_with($stored, '$2y$') || str_starts_with($stored, '$2a$')) {
        return password_verify($password, $stored);
    }
    return hash_equals($stored, pbkdf2_hash($password, (string)$user['salt']));
}

function auth_login(Db $db, array $config, array $body): never
{
    $username = trim((string)($body['username'] ?? ''));
    $password = (string)($body['password'] ?? '');
    if ($username === '' || $password === '') {
        fail(422, 'Informe usuário e senha.');
    }

    $user = $db->one(
        'SELECT id, username, display_name, password_hash, salt FROM users WHERE username = ?',
        [$username]
    );
    if (!$user || !auth_verify_password($user, $password)) {
        fail(401, 'Usuário ou senha inválidos.');
    }

    if (!str_starts_with((string)$user['password_hash'], '$2')) {
        $db->q('UPDATE users SET password_hash = ? WHERE id = ?', [
            password_hash($password, PASSWORD_BCRYPT),
            $user['id'],
        ]);
    }

    $ttlDays = (int)($config['tokens']['ttl_days'] ?? 30);
    $token = bin2hex(random_bytes(32));
    $db->q(
        'INSERT INTO api_tokens (user_id, token_hash, expires_at) VALUES (?, ?, DATE_ADD(NOW(), INTERVAL ? DAY))',
        [$user['id'], hash('sha256', $token), $ttlDays]
    );
    $db->q('UPDATE users SET last_login = NOW() WHERE id = ?', [$user['id']]);
    $db->q(
        'INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
        [$user['id'], 'login', 'Sessão iniciada']
    );

    json_out([
        'token' => $token,
        'expires_in_days' => $ttlDays,
        'user' => [
            'id' => (int)$user['id'],
            'username' => $user['username'],
            'display_name' => $user['display_name'],
        ],
    ]);
}

function auth_user(Db $db): array
{
    $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if (!preg_match('/Bearer\s+(\S+)/i', $header, $m)) {
        fail(401, 'Token ausente.');
    }
    $hash = hash('sha256', $m[1]);
    $row = $db->one(
        'SELECT t.id AS token_id, t.expires_at, u.id, u.username, u.display_name'
        . ' FROM api_tokens t JOIN users u ON u.id = t.user_id'
        . ' WHERE t.token_hash = ?',
        [$hash]
    );
    if (!$row || strtotime((string)$row['expires_at']) < time()) {
        fail(401, 'Token inválido ou expirado.');
    }
    $db->q('UPDATE api_tokens SET last_used_at = NOW() WHERE id = ?', [$row['token_id']]);
    unset($row['token_id'], $row['expires_at']);
    $row['id'] = (int)$row['id'];
    return $row;
}

function auth_logout(Db $db, array $user): never
{
    $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if (preg_match('/Bearer\s+(\S+)/i', $header, $m)) {
        $db->q('DELETE FROM api_tokens WHERE token_hash = ?', [hash('sha256', $m[1])]);
    }
    $db->q(
        'INSERT INTO activities (user_id, action, details) VALUES (?, ?, ?)',
        [$user['id'], 'logout', 'Sessão encerrada']
    );
    json_out(['ok' => true]);
}
