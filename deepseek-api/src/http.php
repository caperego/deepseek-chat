<?php
declare(strict_types=1);

function send_cors(): void
{
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Headers: Authorization, Content-Type');
    header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
}

function json_out(mixed $data, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function fail(int $status, string $message): never
{
    json_out(['error' => $message], $status);
}

function read_json_body(): array
{
    $raw = file_get_contents('php://input') ?: '';
    if ($raw === '') {
        return [];
    }
    $data = json_decode($raw, true);
    if (!is_array($data)) {
        fail(400, 'JSON inválido');
    }
    return $data;
}

function path_int(string $path, string $pattern): int
{
    if (!preg_match($pattern, $path, $m)) {
        fail(400, 'ID inválido na URL');
    }
    return (int)$m[1];
}
