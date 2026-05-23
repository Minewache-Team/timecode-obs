<?php
/**
 * MW-Aufnahme System - Discord OAuth2 Auth-Hilfsfunktionen
 */

function mw_build_discord_auth_url(): string
{
    if (session_status() === PHP_SESSION_NONE) { session_start(); }

    if (empty($_SESSION['discord_oauth_state'])) {
        $_SESSION['discord_oauth_state'] = bin2hex(random_bytes(16));
    }
    $state = $_SESSION['discord_oauth_state'];

    return 'https://discord.com/api/oauth2/authorize?' . http_build_query([
        'client_id'     => DISCORD_CLIENT_ID,
        'redirect_uri'  => DISCORD_REDIRECT_URI,
        'response_type' => 'code',
        'scope'         => DISCORD_SCOPES,
        'state'         => $state,
    ]);
}

function mw_discord_post(string $url, array $fields): ?array
{
    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_POST           => true,
        CURLOPT_POSTFIELDS     => http_build_query($fields),
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_HTTPHEADER     => ['Content-Type: application/x-www-form-urlencoded'],
        CURLOPT_TIMEOUT        => 10,
    ]);
    $body = curl_exec($ch);
    curl_close($ch);
    return $body ? json_decode($body, true) : null;
}

function mw_discord_get(string $url, string $token): ?array
{
    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_HTTPHEADER     => ["Authorization: Bearer $token"],
        CURLOPT_TIMEOUT        => 10,
    ]);
    $body = curl_exec($ch);
    curl_close($ch);
    return $body ? json_decode($body, true) : null;
}
