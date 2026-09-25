<?php
declare(strict_types=1);

require __DIR__ . '/src/http.php';
require __DIR__ . '/src/db.php';
require __DIR__ . '/src/auth.php';
require __DIR__ . '/src/deepseek.php';
require __DIR__ . '/src/routes.php';

$configPath = __DIR__ . '/config.php';
if (!is_file($configPath)) {
    json_out(['error' => 'config.php não encontrado'], 500);
}
$config = require $configPath;

send_cors();

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'OPTIONS') {
    http_response_code(204);
    exit;
}

try {
    $db = new Db($config['db']);
    $method = strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET'));
    $path = parse_url((string)($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH) ?: '/';

    $base = rtrim(str_replace('\\', '/', dirname((string)($_SERVER['SCRIPT_NAME'] ?? ''))), '/');
    if ($base !== '' && $base !== '.' && str_starts_with($path, $base)) {
        $path = substr($path, strlen($base));
    }
    $path = rtrim($path, '/');
    if ($path === '') {
        $path = '/';
    }

    if ($path === '/health') {
        json_out(['ok' => true, 'time' => date('c')]);
    }

    if ($path === '/auth/register' && $method === 'POST') {
        auth_register($db, read_json_body());
    }

    if ($path === '/auth/login' && $method === 'POST') {
        auth_login($db, $config, read_json_body());
    }

    $user = auth_user($db);

    if ($path === '/auth/logout' && $method === 'POST') {
        auth_logout($db, $user);
    }

    handle_routes($db, $config, $user, $method, $path, read_json_body());
} catch (PDOException $e) {
    error_log('[deepseek-api] DB: ' . $e->getMessage());
    fail(500, 'Erro de banco de dados.');
} catch (Throwable $e) {
    error_log('[deepseek-api] ' . $e->getMessage());
    fail(500, 'Erro interno do servidor.');
}
